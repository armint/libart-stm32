# libart-stm32

libart-stm32 connects the hardware-independent interfaces of
[libArT](https://gitlab.vandertogt.org/embedded-libraries/libart) to the
STM32 peripheral drivers of
[system-stm32](https://gitlab.vandertogt.org/embedded-libraries/system-stm32).
libArT defines interfaces such as `Readable`, `Writable`, `TemperatureSensor`
and the USB device stack. system-stm32 gives register-level drivers such as
`hw::Usart` and `hw::UsbDevice`. This library implements the libArT
interfaces on these drivers. Thus firmware code can use the libArT interfaces
and does not depend on one STM32 family.

## Contents

| Component | Headers | Function |
|-----------|---------|----------|
| Serial port | `Serial.h`, `SerialReadable.h`, `SerialWritable.h` | Interrupt-driven USART with RX and TX ring buffers. It is a `Readable` and a `Writable`. |
| Blocking serial output | `BlockingSerialWritable.h` | A `Writable` that polls the USART without interrupts. Use it for diagnostics and for the first tests of a new board. |
| TX synchronization | `SerialTxSync.h` | An interface that lets `SerialWritable` block on a real-time operating system (RTOS) while the TX buffer drains. The application supplies the implementation. |
| I2C master | `I2cMaster.h` | Blocking I2C master on `hw::I2c`, with a timeout. |
| Power management | `power/PowerManager.h`, `power/SleepLock.h`, `power/TicklessIdleHook.h` | Reference counts of sleep-mode locks, and a tickless idle hook. The hook does not depend on one RTOS. |
| Temperature sensor | `sensors/InternalTemperatureSensor.h` | A libArT `TemperatureSensor` on the internal temperature sensor of the STM32. |
| USB device | `usb/Stm32UsbDeviceController.h`, `usb/UsbSerial.h` | The USB device controller for the libArT USB stack, and a serial port on the USB Communications Device Class (CDC-ACM). See [USB device controller](#usb-device-controller) and [USB serial](#usb-serial). |

Not all components exist on all families. See [STM32F1](#stm32f1) and
[STM32H5](#stm32h5). The USB components exist only on devices with the USB
full-speed device driver of system-stm32 (see
[USB device controller](#usb-device-controller)). The CI pipeline builds the library for STM32L0,
STM32G0, STM32F1 and STM32H5 (see
[Continuous integration](#continuous-integration)).

## Usage

### Dependencies

The library needs these items:

- [libArT](https://gitlab.vandertogt.org/embedded-libraries/libart), CMake
  target `art::core`.
- [system-stm32](https://gitlab.vandertogt.org/embedded-libraries/system-stm32),
  CMake target `system-stm32`. system-stm32 also supplies the toolchain file
  (`cmake/gcc_arm.cmake`), the start-up code and the linker scripts.
- An `arm-none-eabi` GCC toolchain and CMake 3.20 or later.
- At C++17 only: span-compat. libArT and system-stm32 fetch it (see
  [Language standard](#language-standard)).

### Add the library to a firmware project

Put libArT, system-stm32 and libart-stm32 in the source tree of the firmware
project, for example as git submodules in `dependencies/`. Add libArT and
system-stm32 before this library, because this library links their targets.
Then link your executable with `art::stm32`:

```cmake
cmake_minimum_required(VERSION 3.20)
# Before project(): the toolchain file reads STM32_FAMILY.
set(STM32_FAMILY STM32G0 CACHE STRING "")
set(STM32_DEVICE STM32G071xx CACHE STRING "")
project(my-firmware C CXX ASM)

set(CMAKE_CXX_STANDARD 23)

add_subdirectory(dependencies/libArT)
add_subdirectory(dependencies/system-stm32)
add_subdirectory(dependencies/libart-stm32)

add_executable(firmware.elf src/main.cpp)
target_link_libraries(firmware.elf PRIVATE art::stm32)
```

Configure with the ARM toolchain file of system-stm32 and a build type:

```sh
cmake -B build \
    -D CMAKE_TOOLCHAIN_FILE=dependencies/system-stm32/cmake/gcc_arm.cmake \
    -D GCC_TOOLCHAIN_PREFIX=/opt/arm-gnu-toolchain \
    -D CMAKE_BUILD_TYPE=Release
```

`STM32_FAMILY` and `STM32_DEVICE` are system-stm32 variables. Set them as
cache entries before `project()`, because the toolchain file uses
`STM32_FAMILY` to select the core flags (`-mcpu=...`). If you set them after
`project()`, the build gets the core flags of the default family, and CMake
does not report it. Without a build type, the compiler gets no core flags
and the build fails. The system-stm32 README gives the supported values and
the other settings, for example the clock and the linker script.
`ci/CMakeLists.txt` is a full example of such a superproject.

If the superproject defines an interface target `art-build-config`, the
library compiles with its compile options and definitions. Use it to give
all libraries the same flags, for example `-fno-exceptions`. Define the
target before the `add_subdirectory()` calls. Each library looks for the
target when CMake adds it, and ignores a target that you define later.

### Example: serial port

`Serial` uses ring buffers that the application owns. Set the system clock
first, because `configure()` calculates the baud rate from the USART clock.
Then do these steps:

1. Enable the clock of the USART.
2. Configure the pins and the USART.
3. Connect the USART interrupt to `Serial::handleInterrupt()`.

```cpp
#include <Serial.h>
#include <peripherals/Rcc.h>
#include <utils/Interrupt.h>
#include <utils/RingBuffer.h>

RingBuffer<uint8_t, 64> txBuffer;
RingBuffer<uint8_t, 128> rxBuffer;
Serial serial(USART2, txBuffer, rxBuffer);

void initSerial() {
   Rcc::enableUSART2Clock();
   // Configure the TX and RX pins as alternate function (hw::GpioPin).
   auto &uart = serial.uart();
   uart.configure(115200, Usart::Parity::None, Usart::StopBits::StopBits_1);
   uart.enableTx();
   uart.enableRx();
   uart.enableRxNotEmptyInterrupt();
   uart.enable();

   Interrupt::connectMemberHandler<Serial, &Serial::handleInterrupt>(USART2_IRQn, serial);
   NVIC_EnableIRQ(USART2_IRQn);
}
```

Then use `serial` as any libArT `Readable` or `Writable`, for example as the
`LogManager` sink. For the USB serial port, see [USB serial](#usb-serial).

## Language standard

The minimum standard is C++17. The firmware projects use C++23. The library
does not select a standard. The consumer sets it with `CMAKE_CXX_STANDARD`.

The standard selects the types of `art::expected` and `art::span` (libArT,
`utils/compat/`) through the libArT option `ART_USE_STD_EXPECTED_SPAN`:

| Standard | `ART_USE_STD_EXPECTED_SPAN` | `art::expected` | `art::span` and `hw::span` |
|---|---|---|---|
| C++23 | `ON` (1), the default | `std::expected` | `std::span` |
| C++17 | `OFF` (0) | libArT's own expected | `span_compat::span` |

At C++17, the library needs span-compat (`embedded-libraries/span-compat`).
It gets span-compat through libArT (`art::core`) and system-stm32. This
library has no CMake setting for span-compat. span-compat must use its own
span (`SPAN_COMPAT_USE_STD_SPAN=OFF`). libArT and system-stm32 set this value
when they fetch span-compat.

All libraries in one build must use the same `ART_USE_STD_EXPECTED_SPAN`
value. This applies to libArT, libArT-proto, system-stm32, this library and
the application. `Readable` and `Writable` have virtual functions that use
these types. Thus a mixed build violates the one-definition rule, and the
compiler does not report it. `art-core` exports the value as a public compile
definition. Do not define it in a source file.

system-stm32 selects `hw::span` from `CMAKE_CXX_STANDARD`, not from
`ART_USE_STD_EXPECTED_SPAN`. Thus do not use C++20 or C++23 with
`ART_USE_STD_EXPECTED_SPAN=OFF`. Always set `CMAKE_CXX_STANDARD`. Otherwise,
`art::span` and `hw::span` can be different types. `Stm32UsbDeviceController.h`
has a `static_assert` that stops such a build.

## STM32F1

The library builds for STM32F1 (STM32F103), with the STM32F1 support of
system-stm32. `STM32_FAMILY` (a system-stm32 CMake variable) selects the
family. On `STM32F1`, the build leaves out the sources that need
peripherals that STM32F1 does not have:

| Source | Reason |
|--------|--------|
| `I2cMaster` | It uses the I2C v2 registers. STM32F1 has I2C v1. |
| `power/TicklessIdleHook` | It uses the RTC wake-up timer and Stop 2 mode. |
| `sensors/InternalTemperatureSensor` | It uses the factory calibration values of the temperature sensor. STM32F1 does not have them. |

`power/PowerManager` stays in the build, because `SerialWritable` uses it.
`Serial`, `SerialReadable`, `SerialWritable` and `BlockingSerialWritable`
use the USART v1 driver of system-stm32 and need no change.
`usb/Stm32UsbDeviceController` and `usb/UsbSerial` use the STM32F1
`hw::UsbDevice`.

The CI job `cross-build` builds the library for STM32F103xB (see
[Continuous integration](#continuous-integration)).

## STM32H5

The library builds for STM32H5 (STM32H533), with the STM32H5 support of
system-stm32. `Serial`, `SerialReadable`, `SerialWritable`,
`BlockingSerialWritable` and `sensors/InternalTemperatureSensor` need no
change. `usb/Stm32UsbDeviceController` and `usb/UsbSerial` use the H5
`hw::UsbDevice` (USB DRD block). On `STM32H5`, the build leaves out these
sources:

| Source | Reason |
|--------|--------|
| `I2cMaster` | system-stm32 has no I2C driver for STM32H5. |
| `power/TicklessIdleHook` | `hw::Pwr` cannot select the Stop mode of STM32H5 (`PWR_PMCR`). STM32H5 has no Stop 2 mode. |

### CDC echo image

`examples/nucleo-h533-cdc-echo/` is a test image for the NUCLEO-H533RE. It
does these steps:

- It sets the core clock to 250 MHz from the 24 MHz HSE crystal. Hold B1
  during reset to use HSI.
- It enumerates on USB (CN3) as a CDC-ACM device with the pid.codes test
  ID `1209:0001`. It echoes each byte that it receives.
- It prints the USB state changes on USART2, the ST-LINK virtual COM port
  (115200 8N1). It echoes each byte that it receives there with `Serial`.
- LD2 blinks at 1 Hz while the main loop runs.

`ci/cross-build.sh` builds the image for `STM32_DEVICE=STM32H533xx` at C++20
or later. The image is
`build-ci/STM32H533xx-cpp23/examples/nucleo-h533-cdc-echo/cdc-echo-nucleo-h533.hex`.
For STM32H533xx, `ci/CMakeLists.txt` sets the system-stm32 variable
`SYSTEM_STM32_HSE_VALUE` to 24000000 (the crystal of the NUCLEO-H533RE).
The image uses the start-up code and the linker scripts of system-stm32.

To test the USB echo on Linux, flash the image and connect CN3. Then open
the new port (for example `/dev/ttyACM1`) in a terminal program. The program
must set DTR. Each typed character comes back.

### Hardware test image

`tests/target/nucleo-h533/` is a test image for the NUCLEO-H533RE. It checks
the STM32H5 support of system-stm32 and libart-stm32 on the real chip, and
prints one `PASS` or `FAIL` line per check on the ST-LINK virtual COM port.
`ci/cross-build.sh` builds it with the CDC echo image. The host script
`tests/target/nucleo-h533/hwtest.py` checks the USB part and gives one
result. The [README](tests/target/nucleo-h533/README.md) of the image gives
the jumper wires and the procedure.

## USB device controller

`usb/Stm32UsbDeviceController.h` connects the USB device stack of libArT to
the USB driver of system-stm32. It implements `art::usb::DeviceController`
(libArT) on `hw::UsbDevice` (system-stm32). The class exists only on a device
with the USB full-speed (FS) device driver of system-stm32 (currently
STM32F1, STM32L0 with USB and STM32H5). This driver defines the macro
`SYSTEM_STM32_USBFS`. On other devices, the header and the source compile to
nothing, and the macro `ART_STM32_HAS_USB_DEVICE_CONTROLLER` is not defined.

```cpp
hw::UsbDevice usbHardware;
RingBuffer<hw::usb::Event, 16> usbEvents;
Stm32UsbDeviceController usbController(usbHardware, usbEvents);
art::usb::Device usbDevice(usbController, descriptors);

usbController.begin(); // USB clock must be on (see system-stm32 README)
// Do the long initialization here, before start().
usbDevice.start();     // pull-up on: the host starts enumeration
// In the main loop:
usbController.poll();
```

Call `start()` immediately before the main loop starts to call `poll()`.
After `start()`, the host resets the device and sends SETUP packets. If
`poll()` does not run, the events fill the queue.

### Interrupt and main loop

- The USB interrupt only puts the events of `hw::UsbDevice` into the event
  queue. The queue is a single-producer, single-consumer lock-free ring
  buffer.
- `poll()` takes the events from the queue in the main loop and gives them to
  `usb::Device`. Thus all protocol logic runs in the main loop.
- An OUT packet stays in packet memory until `poll()` gives it to
  `usb::Device`, because the endpoint answers NAK until it is armed again.
- The interrupt writes the address of SET_ADDRESS to the hardware when the
  host acknowledges the status stage. The main loop is too slow for this,
  because the host uses the new address 2 ms later.
- After a bus reset, `poll()` drops the events that came before the reset.
- While a new SETUP packet or a bus reset waits in the queue, the controller
  ignores calls on EP0. They belong to the control transfer that the SETUP
  packet or the reset aborted.
- After `closeEndpoint()`, `poll()` drops the queued packet events of that
  endpoint. They do not reach an endpoint that opens again with the same
  address.
- If an OUT packet of an endpoint waits in the queue, `armOutTransfer()` on
  that endpoint takes effect only after `poll()` delivers the packet. Thus
  the next packet cannot overwrite the packet in packet memory.
- `cancelInTransfer()` sets the IN endpoint to NAK with two writes, as
  `closeEndpoint()` does. All interrupts stay masked between the two writes.
  The data toggle does not change. If the host took the packet before the
  call or during the first write, its InComplete event waits in the queue.
  Then `cancelInTransfer()` returns false, and `poll()` delivers the event.
  The call does not touch a stalled endpoint, and it does not take back an
  EP0 packet.

### Double-buffered OUT endpoint

`setDoubleBufferedOut(address)` makes the controller open one
bulk OUT endpoint with two buffers (see the system-stm32 README,
"Double-buffered bulk OUT"). Call it before `begin()`. The hardware then
receives the next packet while the main loop handles the last packet.
Thus the host gets fewer NAKs, and the OUT transfer rate goes up.

```cpp
usbController.setDoubleBufferedOut(0x03);
usbController.begin();
```

The protocol layer sees no difference. It gets one packet for each
`armOutTransfer()`. When the next packet is already in the second buffer,
`hw::UsbDevice::armOut()` puts its event into the queue at once. This
occurs in the main loop, with the USB interrupt masked. Thus the queue
still has one producer at a time. Each endpoint direction still puts at
most one packet event into the queue.

The IN endpoint with the same number cannot open. The packet memory must
have room for two buffers. The STM32F1 has only 512 bytes of packet memory.
There, give `hw::UsbDevice` a smaller buffer descriptor table if necessary
(constructor argument `endpointCount`).

### Queue size

An endpoint answers NAK until it is armed again. Thus each endpoint
direction puts at most one packet event into the queue. Add SETUP packets and
bus events (reset, suspend, resume). A queue of 16 entries is sufficient for
CDC-ACM and MSC together, while the main loop calls `poll()`.
This estimate is not valid when the main loop is blocked: the host retries the
enumeration and sends more SETUP packets and bus resets.

When the queue is full, the interrupt drops the newest event.
`droppedEvents()` counts the dropped events. The controller cannot recover
from a dropped event:

- A dropped OUT or IN event stops that endpoint until the next bus reset or
  SET_CONFIGURATION.
- A dropped bus reset leaves `usb::Device` in its old state, but the hardware
  is in the reset state.

If `droppedEvents()` increases, recover with a new enumeration:

1. Call `usbDevice.stop()`. The host sees a disconnect.
2. Call `usbController.poll()` to empty the queue.
3. Call `usbDevice.start()`. The host enumerates the device again.

### Limits

- `bMaxPacketSize0` must be 64.
- `closeEndpoint()` sets the endpoint to NAK, because the hardware cannot
  disable one endpoint and keep the others. When the last endpoint other than
  EP0 closes, the controller disables all endpoints and frees their packet
  memory.
- Reopening a closed endpoint with a different `wMaxPacketSize` fails while
  another endpoint other than EP0 is open.

## USB serial

`usb/UsbSerial.h` is a `Readable` and a `Writable` on the CDC-ACM function of
libArT (`art::usb::CdcAcmFunction`). It has the same API as `SerialReadable`
and `SerialWritable`. Thus it can be the `LogManager` sink or the stream of a
console.

```cpp
art::usb::CdcAcmFunction<256, 256> cdc(0, art::usb::CdcAcmEndpoints{ 0x81, 16, 0x02, 0x82 });
UsbSerial usbSerial(cdc);
usbDevice.addFunction(cdc);
```

Nothing in `UsbSerial` waits. The USB events arrive only through
`Stm32UsbDeviceController::poll()` in the main loop, so a wait in the main
loop cannot make progress.

- `read()` returns `Readable::Timeout` at once if no byte is in the RX queue.
- `write()` puts the data into the TX queue and drops the data that does not
  fit. While no terminal has the port open, it drops all data. A terminal is
  open when the host sets DTR and the device is configured. `write()` always
  reports all bytes as written, so a logger does not stop on a full or closed
  port. `droppedBytes()` counts the dropped bytes, and `resetDroppedBytes()`
  sets the count to 0.
- `flush()` returns at once. `poll()` sends the TX queue.
- `write()` can cut a message: it sends the start that fits into the TX
  queue and drops the end. A console that must not send a cut message checks
  `writeAvailable()` for the full message size before it writes. If there is
  not enough room, it tries again in a later main loop iteration.
  `isTxIdle()` tells when the host has read all data.
- The `Readable` helpers (`read(span)`, `readNumber()`, `readRLE()`,
  `readUntil()`, `scanf()`) call `read()` for each byte. They stop at the
  first `Timeout`, and the bytes that they took before are lost. Use them only
  for data that is already in the RX queue: check `readAvailable()` first.
  Otherwise, read byte by byte with `read()`.
- Call `UsbSerial` only from the main loop, the context that calls `poll()`.
  The CDC-ACM function does no locking.

### Terminal sessions

A terminal session starts when the host sets DTR. It ends when the host
clears DTR. The CDC-ACM function does not clear its queues at the end of a
session (libArT issue 48 on the GitLab of the maintainers, not public).
`UsbSerial` handles the RX side, but not the TX side:

- RX: while no terminal is open, `read()` and `available()` discard the
  received data and report no data. Call one of them in each main loop
  iteration. Then the data of an ended session does not reach the next
  session. If the next session starts before such a call, its data comes
  after the old data.
- TX: data that `write()` put into the TX queue before the session ended,
  and that the host did not read, stays in the TX queue. The next terminal
  receives this data before the new data.

## Host tests

`tests/` contains host tests with doctest. Stubs in `tests/stubs` replace the
hardware and some libArT classes. The USB tests use the real libArT USB stack
(`usb::Device`, `usb::CdcAcmFunction`), `Readable` and the fake controller of
the libArT tests (`tests/usb/FakeDeviceController.h`). They find libArT in
`../libArT`, next to this repository. CMake downloads doctest and ETL (the
Embedded Template Library). Set `LIBART_DIR` to use another libArT checkout:

```sh
cmake -S tests -B build-tests -D LIBART_DIR=/path/to/libArT
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

The tests use C++23. Add `-D CMAKE_CXX_STANDARD=17` to run them at C++17.
Then CMake also downloads span-compat, at the version that libArT specifies.

## Continuous integration

Two CI systems run the same jobs. Each clones libArT and system-stm32 (branch
`main`) next to this repository:

| System | File | Runs on | Clones from | Arm GCC |
|---|---|---|---|---|
| GitHub Actions | `.github/workflows/ci.yml` | Each pull request and each push to `main` | github.com/armint | 15.3 (`ghcr.io/armint/firmware-build-image:v1.15.0`) |
| GitLab CI | `.gitlab-ci.yml` | The mirror of `main`, and branches that you push to GitLab | GitLab, with the CI job token | 14.3 (`firmware-builder:v1.14.0`) |

On GitHub, STM32C031xx is a cell of `cross-build` with the same known gap.
The jobs are:

- `unit-tests`: the host tests at C++23 and at C++17.
- `cross-build`: a build of `art-stm32` for STM32L073xx, STM32G071xx,
  STM32G0B1xx, STM32F103xB and STM32H533xx, at C++23 and at C++17. The
  sources of this repository must compile with
  `-Wall -Wextra -Wpedantic -Werror`. The STM32H533xx job at C++23 also
  builds the CDC echo image and the hardware test image (see
  [STM32H5](#stm32h5)), and keeps both `.hex` files as an artifact
  (`nucleo-h533-images` on GitHub).
- `cross-build-c0`: the same build for STM32C031xx. One known gap prevents a
  full build, issue 19 on the GitLab of the maintainers, not public
  (`InternalTemperatureSensor.cpp`). On `STM32C0`, the build leaves out
  `power/TicklessIdleHook`, because STM32C0 has no RTC wake-up timer.
  `InternalTemperatureSensor.cpp` can fail only with the known errors
  (`KNOWN_GAP_PATTERN`). The job fails if this file
  fails with another error, or if another source of `art-stm32` does not
  compile, or if system-stm32 does not build. The job prints a note when the
  gap is fixed.

The CI pipeline uses the latest `main` commit of libArT and system-stm32. The
job log shows the commits that it used.

Each job checks that the build used the intended standard and span type.
`ci/SpanTypeCheck.cpp` checks the types at compile time.

`ci/cross-build.sh` runs a cross build locally, with libArT and system-stm32
next to this repository. The build directory is `build-ci/`:

```sh
STM32_FAMILY=STM32L0 STM32_DEVICE=STM32L073xx CXX_STANDARD=17 \
GCC_TOOLCHAIN_PREFIX=/opt/arm-gnu-toolchain-14.3.rel1-x86_64-arm-none-eabi \
ci/cross-build.sh
```

The script header describes the other variables.

## Contributing

The repository has one branch: `main`. All work, bug fixes too, goes to `main` through a pull request on
[GitHub](https://github.com/armint/libart-stm32). Use a tag or a commit of `main` in your submodule.

## License

libart-stm32 is available under the MIT license. See [LICENSE](LICENSE).
