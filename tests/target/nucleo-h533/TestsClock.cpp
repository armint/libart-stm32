#include "Board.h"
#include "Report.h"
#include "Tests.h"

#include <delaybusywait.h>
#include <peripherals/Rcc.h>
#include <utils/Interrupt.h>
#include <utils/Timekeeping.h>

namespace hwtest {
namespace {

constexpr uint32_t ExpectedCoreHz = Rcc::SysClock250MHz;
constexpr const char *ClockWire = "needs LSE X2 and the wire PA8-PB10";

// Starts LSE (crystal X2) with medium-high drive. LSE is in the backup domain:
// it stays on through a system reset, e.g. the IWDG reset.
bool startLse() {
   PWR->DBPCR |= PWR_DBPCR_DBP;
   if((RCC->BDCR & RCC_BDCR_LSEON) == 0) {
      // LSEDRV can change only while LSE is off.
      RCC->BDCR = (RCC->BDCR & ~RCC_BDCR_LSEDRV) | RCC_BDCR_LSEDRV_1;
      RCC->BDCR |= RCC_BDCR_LSEON;
   }
   // LSE start-up: typically 1 s or less.
   return waitFor([] { return (RCC->BDCR & RCC_BDCR_LSERDY) != 0; }, 3000000);
}

// MCO1 (PA8, AF0) outputs LSE. TIM2 channel 3 (PB10, AF1) captures every 8th
// rising edge with the TIM2 kernel clock, which is pclk1 (no APB prescaler).
void startCapture() {
   RCC->CFGR1 = (RCC->CFGR1 & ~(RCC_CFGR1_MCO1SEL | RCC_CFGR1_MCO1PRE)) | RCC_CFGR1_MCO1SEL_0 | RCC_CFGR1_MCO1PRE_0;
   alternate(GPIOA, 8, Pin::AlternateFunction::AF0);
   alternate(GPIOB, 10, Pin::AlternateFunction::AF1);
   Rcc::enableTIM2Clock();
   TIM2->CR1 = 0;
   TIM2->PSC = 0;
   TIM2->ARR = 0xFFFFFFFFU;
   TIM2->EGR = TIM_EGR_UG;
   TIM2->CCMR2 = TIM_CCMR2_CC3S_0 | TIM_CCMR2_IC3PSC_0 | TIM_CCMR2_IC3PSC_1;
   TIM2->CCER = TIM_CCER_CC3E;
   TIM2->SR = 0;
   TIM2->CR1 = TIM_CR1_CEN;
}

void stopCapture() {
   TIM2->CR1 = 0;
   TIM2->CCER = 0;
   RCC->CFGR1 &= ~(RCC_CFGR1_MCO1SEL | RCC_CFGR1_MCO1PRE);
}

// Waits for the next capture (one every 8 LSE periods, 244 us). Takes the
// TIM2 and the DWT counts of that capture.
bool nextCapture(uint32_t &timerCount, uint32_t &coreCount) {
   if(!waitFor([] { return (TIM2->SR & TIM_SR_CC3IF) != 0; }, 10000)) {
      return false;
   }
   coreCount = cycles();
   timerCount = TIM2->CCR3; // The read clears CC3IF.
   return true;
}

// 128 captures of 8 LSE periods: 1024 / 32768 s = 31.25 ms. Thus a count
// times 32 is the frequency in Hz.
constexpr uint32_t Captures = 128;
constexpr uint32_t CountToHz = LSE_VALUE / (Captures * 8);
static_assert(CountToHz * Captures * 8 == LSE_VALUE);

bool measureAgainstLse(uint32_t &timerHz, uint32_t &coreHz) {
   startCapture();
   uint32_t timer0 = 0, core0 = 0, timer1 = 0, core1 = 0;
   bool ok = nextCapture(timer0, core0);
   for(uint32_t i = 0; ok && i < Captures; ++i) {
      ok = nextCapture(timer1, core1);
   }
   stopCapture();
   timerHz = (timer1 - timer0) * CountToHz;
   coreHz = (core1 - core0) * CountToHz;
   return ok;
}

volatile uint32_t lastVectorHits = 0;

void onLastVector() {
   lastVectorHits = lastVectorHits + 1;
}

struct Counter {
   volatile uint32_t hits = 0;
};
Counter argumentCounter;

void onArgument(void *argument) {
   auto &counter = *static_cast<Counter *>(argument);
   counter.hits = counter.hits + 1;
}

// Pends `irq` and lets the core take it, if it is enabled.
void pend(IRQn_Type irq) {
   NVIC_SetPendingIRQ(irq);
   __DSB();
   __ISB();
}

} // namespace

void testClocks(bool clockOk, bool useHsi) {
   (void)vcp.printf("  SystemCoreClock %lu Hz, APB1 %lu, APB2 %lu, APB3 %lu, PLL1Q %lu\r\n",
                    static_cast<unsigned long>(SystemCoreClock), static_cast<unsigned long>(Rcc::getAPBClock()),
                    static_cast<unsigned long>(Rcc::getAPB2Clock()), static_cast<unsigned long>(Rcc::getAPB3Clock()),
                    static_cast<unsigned long>(Rcc::getPLL1QClock()));
   check("rcc-clock-recipe", clockOk);
   check("rcc-sysclk-is-pll1", (RCC->CFGR1 & RCC_CFGR1_SWS) == RCC_CFGR1_SWS);
   check("rcc-system-core-clock", SystemCoreClock == ExpectedCoreHz);
   check("rcc-bus-clocks", Rcc::getAPBClock() == ExpectedCoreHz && Rcc::getAPB2Clock() == ExpectedCoreHz &&
                              Rcc::getAPB3Clock() == ExpectedCoreHz);
   check("rcc-pll1q-clock", Rcc::getPLL1QClock() == ExpectedCoreHz / 2);
   check("rcc-flash-latency", (FLASH->ACR & FLASH_ACR_LATENCY) == 5U);
   check("icache-enabled", (ICACHE->CR & ICACHE_CR_EN) != 0);
   check("hsi48-ready", Rcc::enableHSI48());

   if(!check("lse-ready", startLse(), "needs the LSE crystal X2")) {
      return;
   }
   uint32_t timerHz = 0, coreHz = 0;
   const bool captured = measureAgainstLse(timerHz, coreHz);
   if(captured) {
      (void)vcp.printf("  measured against LSE: core %lu Hz, TIM2 %lu Hz\r\n", static_cast<unsigned long>(coreHz),
                       static_cast<unsigned long>(timerHz));
   }
   // HSE and LSE have 20 ppm each. The DWT reads add some cycles of polling
   // jitter. HSI has +-0.47 % at 30 C (DS14539), more over temperature.
   const uint32_t ppm = useHsi ? 10000 : 100;
   check("core-clock-vs-lse", captured && within(coreHz, ExpectedCoreHz, ppm + 100), ClockWire);
   check("tim2-clock-vs-lse", captured && within(timerHz, ExpectedCoreHz, ppm), ClockWire);
}

void testTiming() {
   const auto first = getUptimeTicks();
   while(getUptimeTicks() == first) {
   }
   const auto start = getUptimeTicks();
   const uint32_t startCycles = cycles();
   while(getUptimeTicks() - start < 100) {
   }
   const uint32_t tickCycles = cycles() - startCycles;
   (void)vcp.printf("  100 SysTick ticks: %lu cycles\r\n", static_cast<unsigned long>(tickCycles));
   check("systick-1khz", within(tickCycles, SystemCoreClock / 10, 1000));

   const uint32_t delayStart = cycles();
   delayuS(1000);
   const uint32_t delayCycles = cycles() - delayStart;
   (void)vcp.printf("  delayuS(1000): %lu cycles\r\n", static_cast<unsigned long>(delayCycles));
   check("delayus-1000", within(delayCycles, SystemCoreClock / 1000, 5000));
}

void testInterrupts() {
   // I3C2_ER (IRQ 132) is the last entry of the 149-entry vector table.
   Interrupt::connectHandler(I3C2_ER_IRQn, &onLastVector);
   check("irq-vector-table-in-sram", (SCB->VTOR & 0xFF000000U) == 0x20000000U);
   NVIC_EnableIRQ(I3C2_ER_IRQn);
   pend(I3C2_ER_IRQn);
   check("irq-last-vector", lastVectorHits == 1 && Interrupt::getHandler(I3C2_ER_IRQn) == &onLastVector);

   bool blocked = false;
   {
      const Interrupt::BlockingSection section(I3C2_ER_IRQn);
      pend(I3C2_ER_IRQn);
      blocked = lastVectorHits == 1 && NVIC_GetEnableIRQ(I3C2_ER_IRQn) == 0;
   }
   __DSB();
   __ISB();
   check("irq-blocking-section-above-127", blocked && lastVectorHits == 2 && NVIC_GetEnableIRQ(I3C2_ER_IRQn) != 0);
   NVIC_DisableIRQ(I3C2_ER_IRQn);
   Interrupt::releaseHandler(I3C2_ER_IRQn);

   Interrupt::connectHandler(I3C2_EV_IRQn, &onArgument, &argumentCounter);
   NVIC_EnableIRQ(I3C2_EV_IRQn);
   pend(I3C2_EV_IRQn);
   check("irq-handler-with-argument", argumentCounter.hits == 1);
   NVIC_DisableIRQ(I3C2_EV_IRQn);
   Interrupt::releaseHandler(I3C2_EV_IRQn);
}

} // namespace hwtest
