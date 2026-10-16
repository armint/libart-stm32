#pragma once

// Test groups of the hardware test image. Each group prints its results with
// check() (Report.h). main.cpp runs them in this order.

namespace hwtest {

// RCC: the 250 MHz recipe, the bus clocks, ICACHE, and the core clock and the
// TIM2 clock measured against LSE (wire PA8-PB10). clockOk: the result of
// Rcc::setClock250MhzHSE() or Rcc::setClock250MhzHSI().
void testClocks(bool clockOk);

// SysTick and delayuS() against the DWT cycle counter.
void testTiming();

// Interrupt::connectHandler() on the last vector, the handler with an
// argument, and BlockingSection on an IRQ number above 127.
void testInterrupts();

} // namespace hwtest
