#pragma once

#include <cstdarg>
#include <cstddef>
#include <expected>

#include <etl/circular_buffer.h>

#include <cmsis_device.h>

#include <peripherals/Uart.h>
#include <streams/Readable.h>
#include <streams/Writable.h>

/**
 * This class is used to write data to, and read data from a uart. It uses interrupts and buffers to minimize the load
 * on the processor and to make sure real-time processes are not disturbed.
 */
class Serial : public Writable, public Readable {

   protected:
   Uart usart;
   etl::circular_buffer_ext<uint8_t> txBuffer;
   etl::circular_buffer_ext<uint8_t> rxBuffer;

   virtual std::expected<void, Writable::Error> flush(size_t count);

   public:
   // The rule of five.
   Serial(const Serial &) = delete;
   Serial(Serial &&) = delete;
   Serial &operator=(const Serial &) = delete;
   Serial &operator=(Serial &&) = delete;

   Serial(USART_TypeDef *usartTypeDef, std::span<uint8_t> txBuffer, std::span<uint8_t> rxBuffer);

   ~Serial() = default;

   virtual std::expected<size_t, Writable::Error> write(std::span<const uint8_t> data) override;
   virtual std::expected<void, Writable::Error> flush() override;

   void handleInterrupt();
};
