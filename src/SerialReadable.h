#pragma once

#include <utils/compat/expected.h>
#include <utils/compat/span.h>

#include <utils/RingBuffer.h>

#include <peripherals/Usart.h>

#include "streams/BufferedReadable.h"

using hw::Usart;

class SerialReadable : public BufferedReadable {
   protected:
   Usart &uart_;
   RingBufferBase<uint8_t> &rxBuffer;
   uint32_t read_timeout = 10; // fairly arbitrary default value

   // Position tracking for receiver timeout (EndOfMessage) and break detection.
   // Each uses an externally provided buffer of absolute stream positions.
   // A feature is enabled when its buffer pointer is non-null.
   RingBufferBase<uint16_t> *messageEndPositions_;
   RingBufferBase<uint16_t> *breakPositions_;
   uint16_t writePos_ = 0;
   uint16_t readPos_ = 0;

   public:
   explicit SerialReadable(Usart &uart, RingBufferBase<uint8_t> &rxBuffer,
                            RingBufferBase<uint16_t> *messageEndBuffer = nullptr,
                            RingBufferBase<uint16_t> *breakBuffer = nullptr);
   ~SerialReadable() override = default;

   art::expected<char, Error> read() override;
   bool available() override;

   void set_read_timeout(uint32_t timeout) {
      read_timeout = timeout;
   }

   [[nodiscard]] bool messageEndDetectionEnabled() const { return messageEndPositions_ != nullptr; }
   [[nodiscard]] bool breakDetectionEnabled() const { return breakPositions_ != nullptr; }

   // To be called from the device ISR
   void handleRxInterrupt();

   void clear() override;
};
