#pragma once

#include <peripherals/Usart.h>
#include <streams/Writable.h>

// Blocking serial writable — polls TXE instead of using interrupts.
// Suitable for diagnostics and early bring-up where simplicity matters.
class BlockingSerialWritable : public Writable {
   hw::Usart uart_;

   public:
   explicit BlockingSerialWritable(USART_TypeDef *peripheral)
      : uart_(peripheral) {
   }

   hw::Usart &uart() {
      return uart_;
   }

   void initialize(uint32_t baudRate = 115200);

   std::expected<size_t, Error> write(std::span<const uint8_t> data) override;
   std::expected<void, Error> flush() override;
};
