#include "Serial.h"

Serial::Serial(USART_TypeDef *usartTypeDef, std::span<uint8_t> txBuf, std::span<uint8_t> rxBuf)
   : SerialWritable(usart, txBuf)
   , SerialReadable(usart, rxBuf)
   , usart(usartTypeDef) {}

Serial::Serial(USART_TypeDef *usartTypeDef, std::span<uint8_t> txBuf, std::span<uint8_t> rxBuf,
               std::span<uint16_t> messageEndBuf)
   : SerialWritable(usart, txBuf)
   , SerialReadable(usart, rxBuf, messageEndBuf)
   , usart(usartTypeDef) {}

Serial::Serial(USART_TypeDef *usartTypeDef, std::span<uint8_t> txBuf, std::span<uint8_t> rxBuf,
               std::span<uint16_t> messageEndBuf, std::span<uint16_t> breakBuf)
   : SerialWritable(usart, txBuf)
   , SerialReadable(usart, rxBuf, messageEndBuf, breakBuf)
   , usart(usartTypeDef) {}

void Serial::handleInterrupt() {
   handleTxInterrupt();
   handleRxInterrupt();
   usart.clearAllStatusFlags();
}
