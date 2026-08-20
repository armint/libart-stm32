#pragma once

#include <expected>
#include <span>

#include <utils/RingBuffer.h>

#include <peripherals/Usart.h>
#include <power/PowerManager.h>
#include <streams/Writable.h>

using hw::Usart;

class SerialWritable : public Writable {
protected:
  Usart &uart_;
  RingBufferBase<uint8_t> &txBuffer;
  bool txLockHeld_{false};

  std::expected<void, Error> flush(size_t count);

public:
  SerialWritable(Usart &uart, RingBufferBase<uint8_t> &txBuffer);
  ~SerialWritable() override;

  std::expected<size_t, Error> write(std::span<const uint8_t> data) override;
  std::expected<void, Error> flush() override;

  // To be called from the device ISR
  void handleTxInterrupt();
};
