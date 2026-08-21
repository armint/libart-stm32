#include "BlockingSerialWritable.h"

using hw::Usart;

void BlockingSerialWritable::initialize(uint32_t baudRate) {
   uart_.configure(baudRate, Usart::Parity::None, Usart::StopBits::StopBits_1);
   uart_.enableTx();
   uart_.enable();
}

std::expected<size_t, Writable::Error> BlockingSerialWritable::write(std::span<const uint8_t> data) {
   for(auto byte : data) {
      while(!uart_.isTxRegisterEmpty()) {
      }
      uart_.setData(byte);
   }
   return data.size();
}

std::expected<void, Writable::Error> BlockingSerialWritable::flush() {
   while(!uart_.isTxRegisterEmpty() || !uart_.isTransferComplete()) {
   }
   return {};
}
