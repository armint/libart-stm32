#pragma once

#include <cmsis_device.h>

#include "SerialReadable.h"
#include "SerialWritable.h"

using hw::Usart;

class Serial : public SerialWritable, public SerialReadable {
   protected:
   Usart usart;

   public:
   // The rule of five.
   Serial(const Serial &) = delete;
   Serial(Serial &&) = delete;
   Serial &operator=(const Serial &) = delete;
   Serial &operator=(Serial &&) = delete;

   Serial(USART_TypeDef *usartTypeDef,
          RingBufferBase<uint8_t> &txBuffer,
          RingBufferBase<uint8_t> &rxBuffer,
          RingBufferBase<uint16_t> *messageEndBuffer = nullptr,
          RingBufferBase<uint16_t> *breakBuffer = nullptr);

   ~Serial() override = default;

   // Combined ISR handler
   void handleInterrupt();

   Usart &uart() {
      return usart;
   }
};
