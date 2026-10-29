# STM32F1

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
[Tests and CI](testing.md#continuous-integration)).
