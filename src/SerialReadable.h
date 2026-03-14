#pragma once

#include <expected>
#include <span>

#include <etl/circular_buffer.h>

#include <peripherals/Usart.h>

#include "streams/BufferedReadable.h"

class SerialReadable : public BufferedReadable {
   protected:
   Usart &uart_;
   etl::circular_buffer_ext<uint8_t> rxBuffer;
   uint32_t read_timeout = 10; // fairly arbitrary default value

   // Position tracking for receiver timeout (EndOfMessage) and break detection.
   // Each uses an externally provided buffer of absolute stream positions.
   // A feature is enabled when its buffer has capacity > 0.
   etl::circular_buffer_ext<uint16_t> messageEndPositions_;
   etl::circular_buffer_ext<uint16_t> breakPositions_;
   uint16_t writePos_ = 0;
   uint16_t readPos_ = 0;

   public:
   explicit SerialReadable(Usart &uart, std::span<uint8_t> rxBuffer);
   SerialReadable(Usart &uart, std::span<uint8_t> rxBuffer, std::span<uint16_t> messageEndBuffer);
   SerialReadable(Usart &uart, std::span<uint8_t> rxBuffer, std::span<uint16_t> messageEndBuffer,
                  std::span<uint16_t> breakBuffer);
   ~SerialReadable() override = default;

   std::expected<char, Error> read() override;
   bool available() override;

   void set_read_timeout(uint32_t timeout) {
      read_timeout = timeout;
   }

   [[nodiscard]] bool messageEndDetectionEnabled() const { return messageEndPositions_.max_size() > 0; }
   [[nodiscard]] bool breakDetectionEnabled() const { return breakPositions_.max_size() > 0; }

   // To be called from the device ISR
   void handleRxInterrupt();

   void clear() override;
};
