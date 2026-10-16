# NUCLEO-H533RE hardware test image

This image checks the STM32H5 support of system-stm32 and libart-stm32 on a
NUCLEO-H533RE board. The host tests and the CI builds cannot find faults in
register values, clock settings or errata work-arounds. This image runs the
code on the real chip.

## Build

`ci/cross-build.sh` builds the image together with the CDC echo image, for
`STM32_DEVICE=STM32H533xx` at C++20 or later:

```sh
STM32_FAMILY=STM32H5 STM32_DEVICE=STM32H533xx CXX_STANDARD=23 \
GCC_TOOLCHAIN_PREFIX=/opt/arm-gnu-toolchain-15.2.rel1 ci/cross-build.sh
```

The image is
`build-ci/STM32H533xx-cpp23/tests/target/nucleo-h533/hwtest-nucleo-h533.hex`.
The CI job `cross-build` keeps it as an artifact.

## Wiring

Install these jumper wires before you start the test:

| Wire | Pins | Connector pins | Used by |
|------|------|----------------|---------|
| 1 | PA8 – PB10 | D7 – D6 (CN9) | MCO1 (LSE) to TIM2_CH3: clock measurement |
| 2 | PA9 – PA10 | CN10 pin 2 – CN10 pin 4 | GPIO direction switch, USART1 TX to RX, libart-stm32 serial classes |
| 3 | PB15 – PB14 | CN10 pins 26 – 28 | SPI2 MOSI to MISO |

The pins of PA9 and PA10 are for board revision MB1814 C-01. On older
revisions, PA10 is Arduino D2 and PA9 is on another CN10 pin. Refer to
UM3121, the user manual of the board.

A check that needs a missing wire fails. Its `FAIL` line names the wire. The
other checks are not affected.

The test also needs the 32.768 kHz LSE crystal X2 and the user USB port CN3.
Both are on the board in the default configuration.

## Run

The host script `hwtest.py` reads the report of the board and checks the
USB part. It needs Python 3 and pyserial (`pip install pyserial`).

1. Flash the `.hex` file, for example with STM32CubeProgrammer through the
   ST-LINK (CN1).
2. Connect CN1 and CN3 to the host.
3. Start the script: `python3 hwtest.py`. If more than one ST-LINK is
   connected, give the VCP port with `--vcp`, for example
   `--vcp /dev/ttyACM0`.
4. Push the reset button B2 when the script tells you to.

The script prints the board lines with the prefix `board|` and its own checks
with the prefix `host|`. At the end, it prints `RESULT <passed> passed,
<failed> failed` and the name of each failed check. The exit code is 0 only
if all checks pass.

The image sets 250 MHz from the 24 MHz HSE crystal. To check the HSI recipe,
hold the user button B1 while you push B2. Run the script one time for each
recipe. The image reads B1 only at the first start and keeps the choice
through the IWDG reset. The `BANNER` line names the recipe.

### Board output

You can also read the report without the script. Open the ST-LINK virtual
COM port (VCP) at 115200 8N1 and push B2.

The image prints one line per check, `PASS <name>` or `FAIL <name>`, and
then `SUMMARY <passed> passed, <failed> failed`. Lines that start with two
spaces give measured values. The line `UID <hex>` gives the unique device ID.
The USB serial number is the same text.

The independent watchdog (IWDG) check resets the board once. Thus the
`BANNER` line comes two times. The checks start after the second `BANNER`
line.

After the summary, the main loop does these steps:

- It runs a CDC-ACM device on CN3 with the pid.codes test ID `1209:0001`.
  The device echoes each byte that it receives.
- It prints the USB state changes, for example `USB configured` and
  `USB terminal open`.
- It prints the check `crs-sync-usb-sof` 0.5 s after the USB configuration.
- It echoes each byte that it receives on the VCP.
- It reloads an IWDG with a 4 s timeout. LD2 blinks at 1 Hz.

## Checks

| Group | Checks |
|-------|--------|
| IWDG | A 40 ms watchdog resets the board (`RCC_RSR.IWDGRSTF`). `hw::Dbg::freezeIwdgOnDebug()`. The `/1024` prescaler. |
| RCC | Result of the 250 MHz recipe, PLL1 as sysclk, `SystemCoreClock`, APB1 to APB3, PLL1Q (125 MHz), flash latency, ICACHE, HSI48. |
| Clock measurement | LSE starts. The core clock (DWT) and the TIM2 clock, measured against LSE on MCO1. |
| Timing | 100 SysTick ticks and `delayuS(1000)`, measured with the DWT cycle counter. |
| Interrupts | `Interrupt::connectHandler()` on the last vector (I3C2_ER, IRQ 132), the handler with an argument, and `BlockingSection` on an IRQ number above 127. |
| GPIO | PB13 (UCPD CC1) reads low with the pull-up until `hw::Pwr::disableUcpdDeadBattery()`. `GpioPin` output and input. `FastPin::setInput()` and `FastPin::setOutput()`. |
| USART1 | The kernel clock mux (PCLK and HSI) and `getClockFrequency()`. The baud rate (1 Mbit/s), measured with the DWT for each kernel clock. The FIFO: 8 bytes without an overrun. |
| GPDMA memory | A single block. Packing from 8 to 32 bits and unpacking from 32 to 8 bits. A linked list of two items. The transfer complete interrupt. |
| GPDMA with USART1 | 64 bytes TX and RX at the same time. `Channel::abort()` after 16 of 64 bytes, then a new transfer on the same channel. |
| SPI2 | The kernel clock (PLL1Q, 125 MHz). Frames of 8, 16 and 32 bits. `setTransferSize()` with EOT. 64 bytes TX and RX through GPDMA, and the SCK rate of that transfer. |
| ADC1 | Power-up, calibration and enable. VDDA from Vrefint (3.0 V to 3.6 V). The libart-stm32 `InternalTemperatureSensor` (10 °C to 70 °C). A sequence of two ranks through GPDMA. Oversampling x16. |
| libart-stm32 serial | `Serial` on USART1, interrupt-driven: 200 bytes out and back in. `BlockingSerialWritable` on USART1 with the FIFO. `Serial` on USART2 prints the whole report. |
| USB | `usb-start` on the board. On the host: the configuration, the port with the board UID as serial number, DTR open and close, the echo of 1 to 4096 bytes (packet size limits included) and of 64 KiB. |
| CRS | The clock recovery system (CRS) trims HSI48 from the USB start-of-frame packets (`SYNCOKF`). |
| VCP | The host sends a line on the VCP. The `Serial` RX interrupt path of the board echoes it. |

## Not covered

- The HardFault diagnostics: they need a fault.
- `power::PowerManager`: it has no STM32H5-specific code.
- `I2cMaster` and `power::TicklessIdleHook`: the build leaves them out on
  STM32H5.
