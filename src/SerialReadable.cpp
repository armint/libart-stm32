#include "SerialReadable.h"

#include <utils/Timeout.h>

SerialReadable::SerialReadable(Usart &uart, RingBufferBase<uint8_t> &rx,
                                RingBufferBase<uint16_t> *messageEndBuffer,
                                RingBufferBase<uint16_t> *breakBuffer)
   : uart_(uart)
   , rxBuffer(rx)
   , messageEndPositions_(messageEndBuffer)
   , breakPositions_(breakBuffer) {}

std::expected<char, Readable::Error> SerialReadable::read() {
   if(rxBuffer.empty()) {
      utils::Timeout timeout(read_timeout);
      while(rxBuffer.empty() && !timeout) {
         // busy wait until data or timeout
      }
   }
   if(rxBuffer.empty()) {
      return std::unexpected(Timeout);
   }

   // Check for message end (receiver timeout) at this position
   if(messageEndDetectionEnabled()) {
      uint16_t pos = 0;
      if(messageEndPositions_->peek(pos) && pos == readPos_) {
         uint16_t discarded = 0;
         static_cast<void>(messageEndPositions_->try_pop(discarded));
         return std::unexpected(EndOfMessage);
      }
   }

   // Check for break at this position
   if(breakDetectionEnabled()) {
      uint16_t pos = 0;
      if(breakPositions_->peek(pos) && pos == readPos_) {
         uint16_t discarded = 0;
         static_cast<void>(breakPositions_->try_pop(discarded));
         uint8_t b = 0;
         static_cast<void>(rxBuffer.try_pop(b));
         readPos_++;
         return std::unexpected(Break);
      }
   }

   uint8_t b = 0;
   static_cast<void>(rxBuffer.try_pop(b));
   readPos_++;
   return b;
}

bool SerialReadable::available() {
   return !rxBuffer.empty();
}

void SerialReadable::handleRxInterrupt() {
   if(uart_.isRxRegisterNotEmpty() && uart_.isRxNotEmptyInterruptEnabled()) {
      bool isBreak = breakDetectionEnabled() && uart_.isFramingError();
      uint8_t c = uart_.getData();
      if(rxBuffer.full()) {
         // Buffer is full, discard oldest byte and any markers at that position
         if(messageEndDetectionEnabled()) {
            uint16_t pos = 0;
            if(messageEndPositions_->peek(pos) && pos == readPos_) {
               uint16_t discarded = 0;
               static_cast<void>(messageEndPositions_->try_pop(discarded));
            }
         }
         if(breakDetectionEnabled()) {
            uint16_t pos = 0;
            if(breakPositions_->peek(pos) && pos == readPos_) {
               uint16_t discarded = 0;
               static_cast<void>(breakPositions_->try_pop(discarded));
            }
         }
         uint8_t discardedByte = 0;
         static_cast<void>(rxBuffer.try_pop(discardedByte));
         readPos_++;
      }
      static_cast<void>(rxBuffer.try_push(c));
      if(isBreak && c == 0x00) {
         static_cast<void>(breakPositions_->try_push(writePos_));
      }
      writePos_++;
   }

   // Receiver timeout fires after silence on the bus — no data byte, just a marker
   if(messageEndDetectionEnabled() && uart_.isReceiverTimeoutFlag()) {
      static_cast<void>(messageEndPositions_->try_push(writePos_));
      uart_.clearReceiverTimeoutFlag();
   }
}

void SerialReadable::clear() {
   rxBuffer.clear();
   if(messageEndDetectionEnabled()) {
      messageEndPositions_->clear();
   }
   if(breakDetectionEnabled()) {
      breakPositions_->clear();
   }
   readPos_ = writePos_;
}
