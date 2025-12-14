#include "SerialReadable.h"

#include <utils/Timeout.h>

SerialReadable::SerialReadable(Uart &uart, std::span<uint8_t> rx)
   : uart_(uart)
   , rxBuffer(rx.data(), rx.size()) {}

std::expected<char, Readable::Error> SerialReadable::read() {
   if(!rxBuffer.available()) {
      utils::Timeout timeout(read_timeout);
      while(!rxBuffer.available() && !timeout) {
         // busy wait until data or timeout
      }
   }
   if(!rxBuffer.available()) {
      return std::unexpected(Timeout);
   }
   auto b = rxBuffer.front();
   rxBuffer.pop();
   return b;
}

bool SerialReadable::available() {
   return rxBuffer.available();
}

void SerialReadable::handleRxInterrupt() {
   if(uart_.isRxRegisterNotEmpty() && uart_.isRxNotEmptyInterruptEnabled()) {
      uint8_t c = uart_.getData();
      if(rxBuffer.full()) {
         // Buffer is full, discard 1 byte
         rxBuffer.pop();
      }
      rxBuffer.push(c);
   }
}

void SerialReadable::clear() {
   rxBuffer.clear();
}
