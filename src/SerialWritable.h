#pragma once

#include <expected>
#include <span>

#include <etl/circular_buffer.h>

#include <peripherals/Usart.h>
#include <streams/Writable.h>

class SerialWritable : public Writable {
   protected:
   Usart &uart_;
   etl::circular_buffer_ext<uint8_t> txBuffer;

   std::expected<void, Error> flush(size_t count);

   public:
   SerialWritable(Usart &uart, std::span<uint8_t> txBuffer);
   ~SerialWritable() override = default;

   std::expected<size_t, Error> write(std::span<const uint8_t> data) override;
   std::expected<void, Error> flush() override;

   // To be called from the device ISR
   void handleTxInterrupt();
};
