#include "SerialWritable.h"

#include <algorithm>
#include <utils/Timeout.h>

SerialWritable::SerialWritable(Usart &uart, RingBufferBase<uint8_t> &tx)
    : uart_(uart), txBuffer(tx) {}

SerialWritable::~SerialWritable() {
  if (txLockHeld_) {
    power::PowerManager::instance().release(power::SleepMode::Sleep);
    txLockHeld_ = false;
  }
}

std::expected<void, Writable::Error> SerialWritable::flush(size_t count) {
  uart_.disableTxEmptyInterrupt();
  utils::Timeout timeout(5000);
  size_t toFlush = std::min(count, txBuffer.size());
  uint8_t c;
  while (!txBuffer.empty() && !timeout.hasExpired() && toFlush > 0) {
    if (uart_.isTxRegisterEmpty() && txBuffer.try_pop(c)) {
      uart_.setData(c);
      toFlush--;
    }
  }
  if (timeout.hasExpired()) {
    return std::unexpected(CouldNotWrite);
  }
  if (!txBuffer.empty()) {
    if (!txLockHeld_) {
      power::PowerManager::instance().acquire(power::SleepMode::Sleep);
      txLockHeld_ = true;
    }
    uart_.enableTxEmptyInterrupt();
  } else if (txLockHeld_) {
    power::PowerManager::instance().release(power::SleepMode::Sleep);
    txLockHeld_ = false;
  }
  return {};
}

std::expected<void, Writable::Error> SerialWritable::flush() {
  return flush(txBuffer.size());
}

std::expected<size_t, Writable::Error>
SerialWritable::write(std::span<const uint8_t> data) {
  for (size_t i = 0; i < data.size(); i++) {
    uint8_t c = data[i];
    if (txBuffer.full()) {
      auto res = flush(1);
      if (!res) {
        return std::unexpected(res.error());
      }
    }
    if (!txBuffer.try_push(c)) {
      return std::unexpected(CouldNotWrite);
    }
  }
  if (!txBuffer.empty()) {
    if (!txLockHeld_) {
      power::PowerManager::instance().acquire(power::SleepMode::Sleep);
      txLockHeld_ = true;
    }
    uart_.enableTxEmptyInterrupt();
  }
  return data.size();
}

void SerialWritable::handleTxInterrupt() {
  if (uart_.isTxRegisterEmpty() && uart_.isTxEmptyInterruptEnabled()) {
    uint8_t c;
    if (txBuffer.try_pop(c)) {
      uart_.setData(c);
    }
    if (txBuffer.empty()) {
      // Disable transmit buffer empty interrupt
      uart_.disableTxEmptyInterrupt();
      if (txLockHeld_) {
        power::PowerManager::instance().release(power::SleepMode::Sleep);
        txLockHeld_ = false;
      }
    }
  }
}
