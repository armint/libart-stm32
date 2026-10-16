#include "Board.h"
#include "Report.h"
#include "Tests.h"

#include <cstring>

#include <peripherals/Dma.h>
#include <peripherals/Rcc.h>
#include <peripherals/Usart.h>

namespace hwtest {
namespace {

const hw::Usart usart1(USART1);
constexpr uint32_t Baud = 1000000;
constexpr const char *UsartWire = "needs the wire PA9-PA10";

void drainRx() {
   while(usart1.isRxRegisterNotEmpty()) {
      (void)usart1.getData();
   }
   USART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
}

bool waitTransferComplete() {
   return waitFor([] { return usart1.isTransferComplete(); }, 10000);
}

// 1 Mbaud 8N1, TX and RX on. The FIFO setting does not change.
void startUsart1() {
   usart1.disable();
   usart1.configure(Baud);
   usart1.enableTx();
   usart1.enableRx();
   usart1.enable();
   (void)waitTransferComplete(); // TE sends an idle frame first.
   drainRx();
}

void send(const uint8_t *data, size_t count) {
   for(size_t i = 0; i < count; ++i) {
      while(!usart1.isTxRegisterEmpty()) {
      }
      usart1.setData(data[i]);
   }
}

// Sends `count` bytes before it reads one. Without the FIFO, more than two
// bytes give an overrun.
bool burst(const uint8_t *data, size_t count) {
   send(data, count);
   if(!waitTransferComplete()) {
      return false;
   }
   size_t received = 0;
   bool same = true;
   while(usart1.isRxRegisterNotEmpty()) {
      const uint8_t byte = usart1.getData();
      same = same && received < count && byte == data[received];
      ++received;
   }
   const bool overrun = (USART1->ISR & USART_ISR_ORE) != 0;
   drainRx();
   return same && received == count && !overrun;
}

// Core clock cycles to send `count` bytes, from the first start bit to the
// end of the last stop bit. RX is off.
uint32_t timeTransmit(size_t count) {
   usart1.disableRx();
   (void)waitTransferComplete();
   USART1->ICR = USART_ICR_TCCF;
   const uint8_t byte = 0x55;
   const uint32_t start = cycles();
   for(size_t i = 0; i < count; ++i) {
      send(&byte, 1);
   }
   const bool done = waitTransferComplete();
   const uint32_t elapsed = cycles() - start;
   usart1.enableRx();
   drainRx();
   return done ? elapsed : 0;
}

bool checkBaudRate(const char *name, uint32_t ppm) {
   constexpr size_t Bytes = 32;
   const uint32_t elapsed = timeTransmit(Bytes);
   const uint32_t expected = Bytes * 10U * (SystemCoreClock / Baud);
   (void)vcp.printf("  %s: %lu cycles for %u bytes, expected %lu\r\n", name, static_cast<unsigned long>(elapsed),
                    static_cast<unsigned>(Bytes), static_cast<unsigned long>(expected));
   return check(name, within(elapsed, expected, ppm));
}

alignas(4) uint8_t txData[64];
alignas(4) uint8_t rxData[64];

Dma::Config rxConfig() {
   Dma::Config config;
   config.direction = Dma::Direction::PeripheralToMemory;
   config.request = Dma::Request::usart1_rx;
   config.destinationIncrement = true;
   return config;
}

Dma::Config txConfig() {
   Dma::Config config;
   config.direction = Dma::Direction::MemoryToPeripheral;
   config.request = Dma::Request::usart1_tx;
   config.sourceIncrement = true;
   return config;
}

bool waitChannel(const Dma::Channel &channel) {
   return waitFor([&] { return channel.isTransferCompleteFlagSet() || channel.isErrorFlagSet(); }, 10000) &&
          !channel.isErrorFlagSet();
}

void testUsartDma() {
   Rcc::enableGPDMA1Clock();
   for(size_t i = 0; i < sizeof(txData); ++i) {
      txData[i] = static_cast<uint8_t>(i * 13 + 5);
   }
   std::memset(rxData, 0, sizeof(rxData));
   const Dma::Channel rxChannel(GPDMA1, 1);
   const Dma::Channel txChannel(GPDMA1, 2);

   drainRx();
   usart1.enableDmaRx();
   bool ok = rxChannel.start(rxConfig(), address(USART1->RDR), rxData, sizeof(rxData)) &&
             txChannel.start(txConfig(), txData, address(USART1->TDR), sizeof(txData));
   usart1.enableDmaTx();
   ok = ok && waitChannel(rxChannel) && waitChannel(txChannel) && std::memcmp(rxData, txData, sizeof(rxData)) == 0;
   usart1.disableDmaTx();
   check("gpdma-usart1-tx-rx", ok, UsartWire);

   // Abort an RX transfer after 16 of 64 bytes.
   std::memset(rxData, 0, sizeof(rxData));
   drainRx();
   ok = rxChannel.start(rxConfig(), address(USART1->RDR), rxData, sizeof(rxData));
   send(txData, 16);
   ok = ok && waitTransferComplete() && waitFor([&] { return rxChannel.remainingBytes() == 48; }, 1000);
   (void)vcp.printf("  gpdma abort: %u bytes left\r\n", static_cast<unsigned>(rxChannel.remainingBytes()));
   rxChannel.abort();
   check("gpdma-abort", ok && rxChannel.isFree() && std::memcmp(rxData, txData, 16) == 0, UsartWire);

   // The channel takes a new transfer after the abort.
   std::memset(rxData, 0, sizeof(rxData));
   ok = rxChannel.start(rxConfig(), address(USART1->RDR), rxData, 8);
   send(txData + 8, 8);
   ok = ok && waitChannel(rxChannel) && std::memcmp(rxData, txData + 8, 8) == 0;
   check("gpdma-start-after-abort", ok, UsartWire);
   usart1.disableDmaRx();
}

} // namespace

void testUsart() {
   Rcc::enableUSART1Clock();
   alternate(GPIOA, 9, Pin::AlternateFunction::AF7);
   alternate(GPIOA, 10, Pin::AlternateFunction::AF7, Pin::Pull::Up);

   usart1.disable();
   hw::Usart::setUSART1ClockSource(hw::Usart::ClockSource::PCLK);
   check("usart1-kernel-clock-pclk", usart1.getClockFrequency() == Rcc::getAPB2Clock());
   startUsart1();
   const uint8_t pattern[8] = { 0x55, 0xA5, 0x00, 0xFF, 0x0F, 0xF0, 0x81, 0x7E };
   check("usart1-loopback", burst(pattern, 2), UsartWire);
   checkBaudRate("usart1-baud-rate-pclk", 10000);

   usart1.disable();
   hw::Usart::setUSART1ClockSource(hw::Usart::ClockSource::HSIKER);
   check("usart1-kernel-clock-hsi", usart1.getClockFrequency() == Rcc::getHSIClock());
   startUsart1();
   // HSI: +-1 % factory trim, more over temperature.
   checkBaudRate("usart1-baud-rate-hsi", 30000);
   usart1.disable();
   hw::Usart::setUSART1ClockSource(hw::Usart::ClockSource::PCLK);

   usart1.enableFifo();
   startUsart1();
   check("usart1-fifo-enabled", usart1.isFifoEnabled());
   check("usart1-fifo-8-byte-burst", burst(pattern, sizeof(pattern)), UsartWire);
   testUsartDma();

   usart1.disable();
   usart1.disableFifo();
}

} // namespace hwtest
