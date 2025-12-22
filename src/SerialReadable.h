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

   public:
   explicit SerialReadable(Usart &uart, std::span<uint8_t> rxBuffer);
   ~SerialReadable() override = default;

   std::expected<char, Error> read() override;
   bool available() override;

   void set_read_timeout(uint32_t timeout) {
      read_timeout = timeout;
   }

   // To be called from the device ISR
   void handleRxInterrupt();

   void clear() override;
};
