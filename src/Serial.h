#pragma once

#include <cmsis_device.h>

#include "SerialReadable.h"
#include "SerialWritable.h"

class Serial : public SerialWritable, public SerialReadable {
   protected:
   Uart usart;

   public:
   // The rule of five.
   Serial(const Serial &) = delete;
   Serial(Serial &&) = delete;
   Serial &operator=(const Serial &) = delete;
   Serial &operator=(Serial &&) = delete;

   Serial(USART_TypeDef *usartTypeDef, std::span<uint8_t> txBuffer, std::span<uint8_t> rxBuffer);

   ~Serial() override = default;

   // Combined ISR handler
   void handleInterrupt();
};
