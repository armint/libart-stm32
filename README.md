# LibArt-STM32

LibArT implementations specifically for stm32 targets

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

## USB device controller

`usb/Stm32UsbDeviceController.h` connects the USB device stack of libArT to
the USB driver of system-stm32. It implements `art::usb::DeviceController`
(libArT) on `hw::UsbDevice` (system-stm32). The class exists only on a family
that has a system-stm32 USB driver (currently STM32L0 with USB and STM32F1).
On other
families, the header and the source compile to nothing, and the macro
`ART_STM32_HAS_USB_DEVICE_CONTROLLER` is not defined.

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
session ([libart#48](https://gitlab.vandertogt.org/embedded-libraries/libart/-/issues/48)).
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

The CI pipeline (`.gitlab-ci.yml`) clones libArT and system-stm32 (branch
`dev`) next to this repository. It has these jobs:

- `unit-tests`: the host tests at C++23 and at C++17.
- `cross-build`: a build of `art-stm32` for STM32L073xx, STM32G071xx,
  STM32G0B1xx and STM32F103xB, at C++23 and at C++17. The sources of this repository must
  compile with `-Wall -Wextra -Wpedantic -Werror`.
- `cross-build-c0`: the same build for STM32C031xx. Two known gaps prevent a
  full build:
  [system-stm32#27](https://gitlab.vandertogt.org/embedded-libraries/system-stm32/-/issues/27)
  (system-stm32 does not build for C0) and
  [#19](https://gitlab.vandertogt.org/embedded-libraries/libart-stm32/-/issues/19)
  (`InternalTemperatureSensor.cpp`). `InternalTemperatureSensor.cpp` can fail
  only with the known errors (`KNOWN_GAP_PATTERN`). The job fails if this file
  fails with another error, or if another source of `art-stm32` does not
  compile. The job prints a note when a gap is fixed.

The CI pipeline uses the latest `dev` commit of libArT and system-stm32. The
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
