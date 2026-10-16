// Hardware test image for the NUCLEO-H533RE. It checks the STM32H5 support
// of system-stm32 and libart-stm32 on the real chip. See README.md in this
// directory for the jumper wires and for hwtest.py, the host side.
//
// It sets 250 MHz from the 24 MHz HSE crystal. Hold B1 (PC13) during reset
// to use the HSI recipe instead. Then it runs the test groups of Tests.h and
// prints one line per check on the ST-LINK virtual COM port (USART2, 115200
// 8N1). The IWDG test resets the board once. LD2 (PA5) blinks at 1 Hz in the
// main loop, which reloads a 4 s IWDG.
#include "Board.h"
#include "Report.h"
#include "Tests.h"

#include <cmsis_device.h>

#include <peripherals/Iwdg.h>
#include <peripherals/Rcc.h>
#include <utils/Timekeeping.h>
#include <utils/timestamp.h>

static_assert(HSE_VALUE == 24000000UL, "the NUCLEO-H533RE has a 24 MHz crystal (SYSTEM_STM32_HSE_VALUE)");

using namespace hwtest;

// The platform hook of libArT (utils/timestamp.h). SysTick runs at 1 kHz.
timestamp_t timestamp_in_ms() {
   return getUptimeTicks();
}

int main() {
   Rcc::enableGPIOAClock();
   Rcc::enableGPIOBClock();
   Rcc::enableGPIOCClock();
   const Pin led(GPIOA, 5);
   led.configure(Pin::Mode::Output, Pin::OutputType::PushPull);
   const Pin key(GPIOC, 13);
   key.configure(Pin::Mode::Input);

   const bool useHsi = key.read();
   const bool clockOk = useHsi ? Rcc::setClock250MhzHSI() : Rcc::setClock250MhzHSE();
   Rcc::enableICache();
   startCycleCounter();
   SysTick_Config(SystemCoreClock / 1000U);

   startVcp();
   (void)vcp.printf("\r\nBANNER libart-stm32 NUCLEO-H533RE hardware test, %s recipe\r\n", useHsi ? "HSI" : "HSE");

   testWatchdogReset();
   testClocks(clockOk);
   testTiming();
   testInterrupts();
   testGpio();
   testUsart();
   startWatchdog();
   printSummary();

   const hw::Iwdg iwdg(IWDG);
   timestamp_t nextBlink = getUptimeTicks() + 500U;
   for(;;) {
      if(getUptimeTicks() >= nextBlink) {
         nextBlink += 500U;
         led.write(!led.read());
         iwdg.reload();
      }
   }
}
