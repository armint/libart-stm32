#include "SerialWritable.h"

#include <algorithm>
#include <utils/Timeout.h>

static constexpr uint32_t TX_TIMEOUT_SPINS = 100'000u;

SerialWritable::SerialWritable(Usart &uart, RingBufferBase<uint8_t> &tx)
    : uart_(uart), txBuffer(tx) {}

SerialWritable::~SerialWritable() {
  uart_.disableTxEmptyInterrupt();
  uart_.disableTxCompleteInterrupt();
}

std::expected<void, Writable::Error> SerialWritable::flush(size_t count) {
  uart_.disableTxEmptyInterrupt();
  uart_.disableTxCompleteInterrupt();
  utils::Timeout timeout(5000);
  size_t toFlush = std::min(count, txBuffer.size());
  uint8_t c;
  bool timedOut = false;
  while (!txBuffer.empty() && !timeout.hasExpired() && toFlush > 0) {
    uint32_t spins = 0;
    while (!uart_.isTxRegisterEmpty()) {
      if (timeout.hasExpired() || ++spins >= TX_TIMEOUT_SPINS) {
        timedOut = true;
        break;
      }
    }
    if (timedOut) {
      break;
    }
    if (txBuffer.try_pop(c)) {
      uart_.setData(c);
      toFlush--;
    }
  }
  if (timedOut || timeout.hasExpired()) {
    return std::unexpected(CouldNotWrite);
  }
  if (!txBuffer.empty()) {
    if (!txSleepLock_) {
      txSleepLock_.emplace(power::SleepMode::Sleep);
    }
    uart_.enableTxEmptyInterrupt();
  } else {
    // txBuffer is empty, but data may still be transmitting in TDR / shift register
    if (txSleepLock_ || !uart_.isTransferComplete()) {
      if (!txSleepLock_) {
        txSleepLock_.emplace(power::SleepMode::Sleep);
      }
      uart_.clearTxCompleteFlag();
      uart_.enableTxCompleteInterrupt();
    }
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
    if (!txSleepLock_) {
      txSleepLock_.emplace(power::SleepMode::Sleep);
    }
    uart_.disableTxCompleteInterrupt();
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
      // Software ring buffer is drained. Disable TXE and enable TC interrupt
      // so the sleep lock is held until the last byte(s) completely leave
      // the hardware TDR and shift register on the TX line.
      uart_.disableTxEmptyInterrupt();
      uart_.clearTxCompleteFlag();
      uart_.enableTxCompleteInterrupt();
    }
  }

  if (uart_.isTransferComplete() && uart_.isTxCompleteInterruptEnabled()) {
    uart_.disableTxCompleteInterrupt();
    uart_.clearTxCompleteFlag();
    if (txBuffer.empty()) {
      txSleepLock_.reset();
    } else {
      uart_.enableTxEmptyInterrupt();
    }
  }
}
