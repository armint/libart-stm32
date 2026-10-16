#pragma once

// Test groups of the hardware test image. Each group prints its results with
// check() (Report.h). main.cpp runs them in this order.

namespace hwtest {

// IWDG: the first start arms a 40 ms watchdog and the board resets. The
// second start checks the IWDG reset flag and the DBG freeze bit. A marker
// in TAMP_BKP0R tells the two starts apart. Run it first.
void testWatchdogReset();

// Starts the IWDG of the main loop (prescaler /1024, 4 s) and checks the
// prescaler. The main loop reloads it with hw::Iwdg::reload().
void startWatchdog();

// RCC: the 250 MHz recipe, the bus clocks, ICACHE, and the core clock and the
// TIM2 clock measured against LSE (wire PA8-PB10). clockOk: the result of
// Rcc::setClock250MhzHSE() or Rcc::setClock250MhzHSI().
void testClocks(bool clockOk);

// SysTick and delayuS() against the DWT cycle counter.
void testTiming();

// Interrupt::connectHandler() on the last vector, the handler with an
// argument, and BlockingSection on an IRQ number above 127.
void testInterrupts();

// GPIO: the UCPD dead-battery disable (PB13), GpioPin and the FastPin
// direction switch (wire PA9-PA10).
void testGpio();

// USART1 (wire PA9-PA10): the kernel clock mux, the baud rate measured with
// PCLK and with HSI, the FIFO, GPDMA TX and RX, and a GPDMA abort.
void testUsart();

} // namespace hwtest
