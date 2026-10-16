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
| 2 | PA9 – PA10 | CN10 | GPIO direction switch, USART1 TX to RX |

A check that needs a missing wire fails. Its `FAIL` line names the wire. The
other checks are not affected.

## Run

1. Flash the `.hex` file, for example with STM32CubeProgrammer through the
   ST-LINK (CN1).
2. Open the ST-LINK virtual COM port at 115200 8N1.
3. Push the reset button B2.

The image sets 250 MHz from the 24 MHz HSE crystal. To check the HSI recipe,
hold the user button B1 while you push B2.

The image prints one line per check, `PASS <name>` or `FAIL <name>`, and
then `SUMMARY <passed> passed, <failed> failed`. Lines that start with two
spaces give measured values. LD2 blinks at 1 Hz after the tests.

The independent watchdog (IWDG) check resets the board once. Thus the
`BANNER` line comes two times. The checks start after the second `BANNER`
line. After the tests, the main loop reloads an IWDG with a 4 s timeout.

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
| GPDMA with USART1 | 64 bytes TX and RX at the same time. `Channel::abort()` after 16 of 64 bytes, then a new transfer on the same channel. |
