#include "Serial.h"

Serial::Serial(USART_TypeDef *usartTypeDef, std::span<uint8_t> txBuf, std::span<uint8_t> rxBuf)
   : SerialWritable(usart, txBuf)
   , SerialReadable(usart, rxBuf)
   , usart(usartTypeDef) {}

void Serial::handleInterrupt() {
   handleTxInterrupt();
   handleRxInterrupt();
   usart.clearAllStatusFlags();
}
