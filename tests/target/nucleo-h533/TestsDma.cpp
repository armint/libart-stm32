#include "Board.h"
#include "Report.h"
#include "Tests.h"

#include <cstring>

#include <peripherals/Dma.h>
#include <peripherals/Rcc.h>
#include <utils/Interrupt.h>

namespace hwtest {
namespace {

alignas(4) uint8_t source[64];
alignas(4) uint8_t target[64];
// The items must share one 64 KiB region: align the array to its size.
alignas(64) Dma::LinkedListItem list[2];

const Dma::Channel channel(GPDMA1, 0);
volatile uint32_t completeInterrupts = 0;

void onComplete() {
   channel.clearTransferCompleteFlag();
   completeInterrupts = completeInterrupts + 1;
}

void prepare() {
   for(size_t i = 0; i < sizeof(source); ++i) {
      source[i] = static_cast<uint8_t>(i * 7 + 1);
   }
   std::memset(target, 0, sizeof(target));
   channel.clearAllFlags();
}

Dma::Config memoryConfig(Dma::DataSize sourceSize, Dma::DataSize destinationSize) {
   Dma::Config config;
   config.sourceSize = sourceSize;
   config.destinationSize = destinationSize;
   config.sourceIncrement = true;
   config.destinationIncrement = true;
   return config;
}

bool copy(Dma::DataSize sourceSize, Dma::DataSize destinationSize, uint16_t bytes) {
   prepare();
   return channel.start(memoryConfig(sourceSize, destinationSize), source, target, bytes) && waitChannel(channel) &&
          std::memcmp(source, target, bytes) == 0;
}

bool linkedList() {
   prepare();
   auto config = memoryConfig(Dma::DataSize::Size32Bits, Dma::DataSize::Size32Bits);
   config.completeEvent = Dma::CompleteEvent::Channel;
   list[1] = Dma::LinkedListItem::make(config, source + 32, target + 32, 32, nullptr);
   list[0] = Dma::LinkedListItem::make(config, source, target, 32, &list[1]);
   return channel.startList(list[0]) && waitChannel(channel) && channel.isFree() &&
          std::memcmp(source, target, sizeof(target)) == 0;
}

bool completeInterrupt() {
   prepare();
   completeInterrupts = 0;
   Interrupt::connectHandler(GPDMA1_Channel0_IRQn, &onComplete);
   NVIC_EnableIRQ(GPDMA1_Channel0_IRQn);
   channel.enableTransferCompleteInterrupt();
   bool ok = channel.start(memoryConfig(Dma::DataSize::Size8Bits, Dma::DataSize::Size8Bits), source, target, 16);
   ok = ok && waitFor([] { return completeInterrupts != 0; }, 10000);
   ok = ok && completeInterrupts == 1 && !channel.isTransferCompleteFlagSet() && std::memcmp(source, target, 16) == 0;
   channel.disableAllInterrupts();
   NVIC_DisableIRQ(GPDMA1_Channel0_IRQn);
   Interrupt::releaseHandler(GPDMA1_Channel0_IRQn);
   return ok;
}

} // namespace

void testDma() {
   Rcc::enableGPDMA1Clock();
   using Size = Dma::DataSize;
   check("gpdma-memory-8bit", copy(Size::Size8Bits, Size::Size8Bits, 32));
   check("gpdma-memory-pack-8-to-32bit", copy(Size::Size8Bits, Size::Size32Bits, 32));
   check("gpdma-memory-unpack-32-to-8bit", copy(Size::Size32Bits, Size::Size8Bits, 32));
   check("gpdma-linked-list", linkedList());
   check("gpdma-complete-interrupt", completeInterrupt());
}

} // namespace hwtest
