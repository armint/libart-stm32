#include "Board.h"
#include "Report.h"
#include "Tests.h"

#include <cstring>
#include <initializer_list>

#include <peripherals/Dma.h>
#include <peripherals/Rcc.h>
#include <peripherals/Spi.h>

namespace hwtest {
namespace {

const hw::Spi spi2(SPI2);
constexpr const char *SpiWire = "needs the wire PB15-PB14";

bool loopback(hw::Spi::DataSize size, std::initializer_list<uint32_t> frames) {
   spi2.setDataSize(size);
   spi2.enable();
   bool ok = true;
   for(const uint32_t frame : frames) {
      const auto received = spi2.transfer(frame);
      ok = ok && received && *received == frame;
   }
   ok = spi2.waitUntilIdle() && ok;
   spi2.disable();
   return ok;
}

// TSIZE = 4: the hardware sets EOT and clears CSTART after 4 frames.
bool transferSize() {
   spi2.setDataSize(hw::Spi::DataSize::Bits8);
   spi2.setTransferSize(4);
   spi2.enable();
   bool ok = true;
   for(const uint32_t frame : { 0x11U, 0x22U, 0x33U, 0x44U }) {
      const auto received = spi2.transfer(frame);
      ok = ok && received && *received == frame;
   }
   ok = waitFor([] { return spi2.isEndOfTransfer(); }, 1000) && ok;
   ok = ok && spi2.remainingFrames() == 0 && !spi2.isTransferStarted();
   spi2.clearAllFlags();
   spi2.disable();
   spi2.setTransferSize(0);
   return ok;
}

alignas(4) uint8_t txData[64];
alignas(4) uint8_t rxData[64];

// 64 frames of 8 bits, TX and RX through GPDMA. Checks the data and the SCK
// rate (125 MHz / 64).
void dmaTransfer() {
   Rcc::enableGPDMA1Clock();
   for(size_t i = 0; i < sizeof(txData); ++i) {
      txData[i] = static_cast<uint8_t>(i * 29 + 3);
   }
   std::memset(rxData, 0, sizeof(rxData));
   Dma::Config rxConfig;
   rxConfig.direction = Dma::Direction::PeripheralToMemory;
   rxConfig.request = Dma::Request::spi2_rx;
   rxConfig.destinationIncrement = true;
   Dma::Config txConfig;
   txConfig.direction = Dma::Direction::MemoryToPeripheral;
   txConfig.request = Dma::Request::spi2_tx;
   txConfig.sourceIncrement = true;
   const Dma::Channel rxChannel(GPDMA1, 3);
   const Dma::Channel txChannel(GPDMA1, 4);

   // RM0481 section 58.4.14: RX DMA on, both channels, TX DMA on, SPE, CSTART.
   spi2.setDataSize(hw::Spi::DataSize::Bits8);
   spi2.setTransferSize(sizeof(txData));
   spi2.enableRxDma();
   bool ok = rxChannel.start(rxConfig, address(SPI2->RXDR), rxData, sizeof(rxData)) &&
             txChannel.start(txConfig, txData, address(SPI2->TXDR), sizeof(txData));
   spi2.enableTxDma();
   spi2.enable();
   const uint32_t start = cycles();
   spi2.startTransfer();
   ok = ok && waitChannel(rxChannel);
   const uint32_t elapsed = cycles() - start;
   ok = ok && waitFor([] { return spi2.isEndOfTransfer(); }, 1000) && std::memcmp(rxData, txData, sizeof(rxData)) == 0;
   spi2.clearAllFlags();
   spi2.disable();
   spi2.disableTxDma();
   spi2.disableRxDma();
   spi2.setTransferSize(0);
   check("gpdma-spi2-tx-rx", ok, SpiWire);

   // 512 bits at 125 MHz / 64. Gaps between frames can only make it slower.
   const uint32_t expected = static_cast<uint32_t>(sizeof(txData) * 8U * 64U * (SystemCoreClock / spi2.getClockFrequency()));
   (void)vcp.printf("  spi2 64 bytes at /64: %lu cycles, expected %lu\r\n", static_cast<unsigned long>(elapsed),
                    static_cast<unsigned long>(expected));
   check("spi2-sck-rate", ok && elapsed >= expected - expected / 50 && elapsed <= 2 * expected, SpiWire);
}

} // namespace

void testSpi() {
   Rcc::enableSPI2Clock();
   alternate(GPIOB, 13, Pin::AlternateFunction::AF5);
   alternate(GPIOB, 14, Pin::AlternateFunction::AF5, Pin::Pull::Down);
   alternate(GPIOB, 15, Pin::AlternateFunction::AF5);

   (void)vcp.printf("  spi2 kernel clock %lu Hz\r\n", static_cast<unsigned long>(spi2.getClockFrequency()));
   check("spi2-kernel-clock-pll1q",
         spi2.getClockFrequency() == Rcc::getPLL1QClock() && spi2.getClockFrequency() == Rcc::SysClock250MHz / 2);

   spi2.configure(hw::Spi::BaudRatePrescaler::Div64);
   spi2.disable();
   check("spi2-loopback-8bit", loopback(hw::Spi::DataSize::Bits8, { 0x5AU, 0x81U, 0x00U, 0xFFU }), SpiWire);
   check("spi2-loopback-16bit", loopback(hw::Spi::DataSize::Bits16, { 0xA55AU, 0x0180U, 0xFFFFU }), SpiWire);
   check("spi2-loopback-32bit", loopback(hw::Spi::DataSize::Bits32, { 0xDEADBEEFU, 0x00000001U, 0x80000000U }), SpiWire);
   check("spi2-transfer-size-eot", transferSize(), SpiWire);
   dmaTransfer();
}

} // namespace hwtest
