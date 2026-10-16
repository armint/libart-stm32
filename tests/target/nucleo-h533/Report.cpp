#include "Report.h"

#include "Board.h"

#include <peripherals/Rcc.h>
#include <utils/Interrupt.h>
#include <utils/RingBuffer.h>

namespace hwtest {
namespace {
RingBuffer<uint8_t, 512> vcpTx;
RingBuffer<uint8_t, 64> vcpRx;
uint32_t passed = 0;
uint32_t failed = 0;
} // namespace

Serial vcp(USART2, vcpTx, vcpRx);

void startVcp() {
   Rcc::enableUSART2Clock();
   alternate(GPIOA, 2, Pin::AlternateFunction::AF7);
   alternate(GPIOA, 3, Pin::AlternateFunction::AF7, Pin::Pull::Up);
   auto &uart = vcp.uart();
   uart.configure(115200, Usart::Parity::None, Usart::StopBits::StopBits_1);
   uart.enableTx();
   uart.enableRx();
   uart.enableRxNotEmptyInterrupt();
   uart.enable();
   Interrupt::connectMemberHandler<Serial, &Serial::handleInterrupt>(USART2_IRQn, vcp);
   NVIC_EnableIRQ(USART2_IRQn);
}

bool check(const char *name, bool ok, const char *hint) {
   if(ok) {
      ++passed;
      (void)vcp.printf("PASS %s\r\n", name);
   } else {
      ++failed;
      (void)vcp.printf("FAIL %s%s%s%s\r\n", name, hint ? " (" : "", hint ? hint : "", hint ? ")" : "");
   }
   return ok;
}

void printSummary() {
   (void)vcp.printf("SUMMARY %lu passed, %lu failed\r\n", static_cast<unsigned long>(passed), static_cast<unsigned long>(failed));
   (void)vcp.flush();
}

} // namespace hwtest
