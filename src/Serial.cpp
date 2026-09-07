#include "Serial.h"

Serial::Serial(USART_TypeDef *usartTypeDef, RingBufferBase<uint8_t> &txBuf, RingBufferBase<uint8_t> &rxBuf,
               RingBufferBase<uint16_t> *messageEndBuf, RingBufferBase<uint16_t> *breakBuf, SerialTxSync *txSync,
               size_t txLowWater)
   : SerialWritable(usart, txBuf, txSync, txLowWater)
   , SerialReadable(usart, rxBuf, messageEndBuf, breakBuf)
   , usart(usartTypeDef) {}

void Serial::handleInterrupt() {
   handleTxInterrupt();
   handleRxInterrupt();
   usart.clearAllStatusFlags();
}
