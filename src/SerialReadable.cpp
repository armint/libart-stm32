#include "SerialReadable.h"

#include <utils/Timeout.h>

SerialReadable::SerialReadable(Usart &uart, std::span<uint8_t> rx)
   : uart_(uart)
   , rxBuffer(rx.data(), rx.size())
   , messageEndPositions_(0)
   , breakPositions_(0) {}

SerialReadable::SerialReadable(Usart &uart, std::span<uint8_t> rx, std::span<uint16_t> messageEndBuf)
   : uart_(uart)
   , rxBuffer(rx.data(), rx.size())
   , messageEndPositions_(messageEndBuf.data(), messageEndBuf.size())
   , breakPositions_(0) {}

SerialReadable::SerialReadable(Usart &uart, std::span<uint8_t> rx, std::span<uint16_t> messageEndBuf,
                               std::span<uint16_t> breakBuf)
   : uart_(uart)
   , rxBuffer(rx.data(), rx.size())
   , messageEndPositions_(messageEndBuf.data(), messageEndBuf.size())
   , breakPositions_(breakBuf.data(), breakBuf.size()) {}

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

   // Check for message end (receiver timeout) at this position
   if(messageEndDetectionEnabled() && !messageEndPositions_.empty() && messageEndPositions_.front() == readPos_) {
      messageEndPositions_.pop();
      return std::unexpected(EndOfMessage);
   }

   // Check for break at this position
   if(breakDetectionEnabled() && !breakPositions_.empty() && breakPositions_.front() == readPos_) {
      breakPositions_.pop();
      rxBuffer.pop();
      readPos_++;
      return std::unexpected(Break);
   }

   auto b = rxBuffer.front();
   rxBuffer.pop();
   readPos_++;
   return b;
}

bool SerialReadable::available() {
   return rxBuffer.available();
}

void SerialReadable::handleRxInterrupt() {
   if(uart_.isRxRegisterNotEmpty() && uart_.isRxNotEmptyInterruptEnabled()) {
      bool isBreak = breakDetectionEnabled() && uart_.isFramingError();
      uint8_t c = uart_.getData();
      if(rxBuffer.full()) {
         // Buffer is full, discard oldest byte and any markers at that position
         if(!messageEndPositions_.empty() && messageEndPositions_.front() == readPos_) {
            messageEndPositions_.pop();
         }
         if(!breakPositions_.empty() && breakPositions_.front() == readPos_) {
            breakPositions_.pop();
         }
         rxBuffer.pop();
         readPos_++;
      }
      rxBuffer.push(c);
      if(isBreak && c == 0x00) {
         breakPositions_.push(writePos_);
      }
      writePos_++;
   }

   // Receiver timeout fires after silence on the bus — no data byte, just a marker
   if(messageEndDetectionEnabled() && uart_.isReceiverTimeoutFlag()) {
      messageEndPositions_.push(writePos_);
      uart_.clearReceiverTimeoutFlag();
   }
}

void SerialReadable::clear() {
   rxBuffer.clear();
   messageEndPositions_.clear();
   breakPositions_.clear();
   readPos_ = writePos_;
}
