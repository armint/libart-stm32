#include "Serial.h"

Serial::Serial(USART_TypeDef *usartTypeDef, RingBufferBase<uint8_t> &txBuf, RingBufferBase<uint8_t> &rxBuf,
               RingBufferBase<uint16_t> *messageEndBuf, RingBufferBase<uint16_t> *breakBuf)
   : SerialWritable(usart, txBuf)
   , SerialReadable(usart, rxBuf, messageEndBuf, breakBuf)
   , usart(usartTypeDef) {}

void Serial::handleInterrupt() {
   handleTxInterrupt();
   handleRxInterrupt();
   usart.clearAllStatusFlags();
}
