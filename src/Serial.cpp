#include "Serial.h"

Serial::Serial(USART_TypeDef *usartTypeDef, std::span<uint8_t> txBuffer, std::span<uint8_t> rxBuffer)
   : usart(usartTypeDef)
   , txBuffer(txBuffer.data(), txBuffer.size())
   , rxBuffer(rxBuffer.data(), rxBuffer.size()) {}


void Serial::handleInterrupt() {
   if(usart.isTxRegisterEmpty() && usart.isTxEmptyInterruptEnabled()) {
      if(!txBuffer.empty()) {
         usart.setData(txBuffer.front());
         txBuffer.pop();
      }
      if(txBuffer.empty()) {
         // Disable transmit buffer empty interrupt
         usart.disableTxEmptyInterrupt();
      }
   }
   if(usart.isRxRegisterNotEmpty() && usart.isRxNotEmptyInterruptEnabled()) {
      uint8_t c = usart.getData();
      if(rxBuffer.full()) {
         // Buffer is full, discard 1 byte
         rxBuffer.pop();
      }
      rxBuffer.push(c);
   }
   usart.clearAllStatusFlags();
}


std::expected<void, Writable::Error> Serial::flush(size_t count) {
   usart.disableTxEmptyInterrupt();
   utils::Timeout timeout(5000);
   size_t toFlush = std::min(count, txBuffer.size());
   while(!txBuffer.empty() && !timeout.hasExpired() && toFlush > 0) {
      if(usart.isTxRegisterEmpty()) {
         usart.setData(txBuffer.front());
         txBuffer.pop();
         toFlush--;
      }
   }
   if(timeout.hasExpired()) {
      return std::unexpected(CouldNotWrite);
   }
   if(!txBuffer.empty()) {
      usart.enableTxEmptyInterrupt();
   }
   return {};
}


std::expected<void, Writable::Error> Serial::flush() {
   return flush(txBuffer.size());
}

std::expected<char, Readable::Error> Serial::read() {
   if(!rxBuffer.available()) {
      utils::Timeout timeout(read_timeout);
      while(!rxBuffer.available() && !timeout) {
         // Wait
      }
   }
   if(!rxBuffer.available()) {
      return std::unexpected(Timeout);
   }
   auto b = rxBuffer.front();
   rxBuffer.pop();
   return b;
}

bool Serial::available() {
   return rxBuffer.available();
}


std::expected<size_t, Writable::Error> Serial::write(std::span<const uint8_t> data) {
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
   usart.enableTxEmptyInterrupt();
   return data.size();
}
