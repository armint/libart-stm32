#include "SerialWritable.h"

#include <algorithm>
#include <utils/Timeout.h>

SerialWritable::SerialWritable(Usart &uart, std::span<uint8_t> tx)
   : uart_(uart)
   , txBuffer(tx.data(), tx.size()) {}

std::expected<void, Writable::Error> SerialWritable::flush(size_t count) {
   uart_.disableTxEmptyInterrupt();
   utils::Timeout timeout(5000);
   size_t toFlush = std::min(count, txBuffer.size());
   while(!txBuffer.empty() && !timeout.hasExpired() && toFlush > 0) {
      if(uart_.isTxRegisterEmpty()) {
         uart_.setData(txBuffer.front());
         txBuffer.pop();
         toFlush--;
      }
   }
   if(timeout.hasExpired()) {
      return std::unexpected(CouldNotWrite);
   }
   if(!txBuffer.empty()) {
      uart_.enableTxEmptyInterrupt();
   }
   return {};
}

std::expected<void, Writable::Error> SerialWritable::flush() {
   return flush(txBuffer.size());
}

std::expected<size_t, Writable::Error> SerialWritable::write(std::span<const uint8_t> data) {
   for(size_t i = 0; i < data.size(); i++) {
      uint8_t c = data[i];
      if(txBuffer.full()) {
         auto res = flush(1);
         if(!res) {
            return std::unexpected(res.error());
         }
      }
      txBuffer.push(c);
   }
   uart_.enableTxEmptyInterrupt();
   return data.size();
}

void SerialWritable::handleTxInterrupt() {
   if(uart_.isTxRegisterEmpty() && uart_.isTxEmptyInterruptEnabled()) {
      if(!txBuffer.empty()) {
         uart_.setData(txBuffer.front());
         txBuffer.pop();
      }
      if(txBuffer.empty()) {
         // Disable transmit buffer empty interrupt
         uart_.disableTxEmptyInterrupt();
      }
   }
}
