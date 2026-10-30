# STM32H5

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

## CDC echo image

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

## Hardware test image

`tests/target/nucleo-h533/` is a test image for the NUCLEO-H533RE. It checks
the STM32H5 support of system-stm32 and libart-stm32 on the real chip, and
prints one `PASS` or `FAIL` line per check on the ST-LINK virtual COM port.
`ci/cross-build.sh` builds it with the CDC echo image. The host script
`tests/target/nucleo-h533/hwtest.py` checks the USB part and gives one
result. The [README](../tests/target/nucleo-h533/README.md) of the image gives
the jumper wires and the procedure.
