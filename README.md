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

## Supported families

| Family | Core | Status |
|--------|------|--------|
| `STM32L0` | Cortex-M0+ | Supported (STM32L073). |
| `STM32G0` | Cortex-M0+ | Supported (STM32G071, STM32G0B1). No USB. |
| `STM32F1` | Cortex-M3 | Supported (STM32F103). No `I2cMaster`, `TicklessIdleHook` and `InternalTemperatureSensor`. See [STM32F1](docs/stm32f1.md). |
| `STM32H5` | Cortex-M33 | Supported (STM32H533). No `I2cMaster` and `TicklessIdleHook`. See [STM32H5](docs/stm32h5.md). |
| `STM32C0` | Cortex-M0+ | Not supported yet. See [Tests and CI](docs/testing.md#continuous-integration). |

`STM32_FAMILY` (a system-stm32 CMake variable) selects the family. The USB
components exist only on devices with the USB full-speed device driver of
system-stm32.

## Components

| Component | Headers | Function |
|-----------|---------|----------|
| Serial port | `Serial.h`, `SerialReadable.h`, `SerialWritable.h` | Interrupt-driven USART with RX and TX ring buffers. It is a `Readable` and a `Writable`. See [Serial port](docs/serial.md). |
| Blocking serial output | `BlockingSerialWritable.h` | A `Writable` that polls the USART without interrupts. Use it for diagnostics and for the first tests of a new board. |
| TX synchronization | `SerialTxSync.h` | An interface that lets `SerialWritable` block on a real-time operating system (RTOS) while the TX buffer drains. The application supplies the implementation. |
| I2C master | `I2cMaster.h` | Blocking I2C master on `hw::I2c`, with a timeout. |
| Power management | `power/PowerManager.h`, `power/SleepLock.h`, `power/TicklessIdleHook.h` | Reference counts of sleep-mode locks, and a tickless idle hook. The hook does not depend on one RTOS. |
| Temperature sensor | `sensors/InternalTemperatureSensor.h` | A libArT `TemperatureSensor` on the internal temperature sensor of the STM32. |
| USB device | `usb/Stm32UsbDeviceController.h`, `usb/UsbSerial.h` | The USB device controller for the libArT USB stack, and a serial port on the USB Communications Device Class (CDC-ACM). See [USB](docs/usb.md). |

## Quick start

Add libArT, system-stm32 and libart-stm32 as git submodules. Then add them
to your CMake project, in this order:

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

The minimum standard is C++17. For the dependencies, the order rules and the
C++ standard, see [Building and using libart-stm32](docs/building.md).

## Repository layout

| Path | Contents |
|------|----------|
| `src/` | The library sources |
| `cmake/` | The CMake package configuration |
| `docs/` | The documentation |
| `examples/` | Example images (CDC echo for the NUCLEO-H533RE) |
| `tests/` | Host tests and the hardware test image of the NUCLEO-H533RE |
| `ci/` | The CI superproject and the cross-build script |

## Documentation

| Document | Contents |
|----------|----------|
| [Building and using libart-stm32](docs/building.md) | Dependencies, CMake integration, `art-build-config`, C++ standard and span type |
| [Serial port](docs/serial.md) | `Serial` set-up example |
| [USB](docs/usb.md) | USB device controller, event queue, limits, USB serial |
| [STM32F1](docs/stm32f1.md) | Sources that the F1 build leaves out |
| [STM32H5](docs/stm32h5.md) | H5 port, CDC echo image, hardware test image |
| [Tests and CI](docs/testing.md) | Host tests, CI jobs, local cross build |

## Contributing

The repository has one branch: `main`. All work, bug fixes too, goes to `main` through a pull request on
[GitHub](https://github.com/armint/libart-stm32). Use a tag or a commit of `main` in your submodule.

## License

libart-stm32 is available under the MIT license. See [LICENSE](LICENSE).
