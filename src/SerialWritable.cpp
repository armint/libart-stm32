#include "SerialWritable.h"

#include <algorithm>
#include <utils/Interrupt.h>
#include <utils/Timeout.h>

// Coarse CPU-cycle ceiling for polling TXE on a single byte. This is the only
// bound available before SysTick starts, so it can't be derived from the
// wall-clock timeout below; it must comfortably outlast one byte's TXE-wait
// time (~1 baud period) for the consumer's baud rate and core clock. Not
// scaled to either, so consumers running very low baud rates and/or high
// core clocks should confirm the margin still holds for their config.
static constexpr uint32_t TX_TIMEOUT_SPINS = 100'000u;

// Wall-clock ceiling on a single flush(), for both the polled and the
// interrupt-driven path.
static constexpr uint32_t FLUSH_TIMEOUT_MS = 5000u;

// A blocked writer re-checks the buffer at least this often. It normally
// wakes on the ISR notification long before this; the slice only bounds the
// case where the drain happened without the TX interrupt running -- a write
// from ISR context or a critical section takes the polled path, which masks
// TXE/TC while it empties the ring, so the notification never comes.
static constexpr uint32_t TX_WAIT_SLICE_MS = 50u;

SerialWritable::SerialWritable(Usart &uart, RingBufferBase<uint8_t> &tx,
                               SerialTxSync *txSync, size_t txLowWater)
    : uart_(uart), txBuffer(tx), txSync_(txSync), txLowWater_(txLowWater) {}

SerialWritable::~SerialWritable() {
  uart_.disableTxEmptyInterrupt();
  uart_.disableTxCompleteInterrupt();
}

void SerialWritable::releaseOrArmTransferComplete() {
  // txBuffer is empty, but data may still be transmitting in TDR / shift
  // register.
  Interrupt::CriticalSection cs;
  if (uart_.isTransferComplete()) {
    // The last byte already finished shifting out (e.g. between draining the
    // buffer and this check) -- release now rather than arming TC and waiting
    // for an edge that will never occur again, since nothing else is queued
    // to transmit.
    txSleepLock_.reset();
  } else if (txSleepLock_) {
    uart_.clearTxCompleteFlag();
    uart_.enableTxCompleteInterrupt();
  }
}

std::expected<void, Writable::Error> SerialWritable::flushPolled(size_t count) {
  {
    Interrupt::CriticalSection cs;
    uart_.disableTxEmptyInterrupt();
    uart_.disableTxCompleteInterrupt();
  }
  utils::Timeout timeout(FLUSH_TIMEOUT_MS);
  size_t toFlush = std::min(count, txBuffer.size());
  uint8_t c;
  bool timedOut = false;
  while (!txBuffer.empty() && !timeout.hasExpired() && toFlush > 0) {
    uint32_t spins = 0;
    while (!uart_.isTxRegisterEmpty()) {
      if (++spins >= TX_TIMEOUT_SPINS ||
          ((spins & 0xFFu) == 0 && timeout.hasExpired())) {
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
    txSleepLock_.reset();
    return std::unexpected(CouldNotWrite);
  }
  if (!txBuffer.empty()) {
    Interrupt::CriticalSection cs;
    if (!txSleepLock_) {
      txSleepLock_.emplace(power::SleepMode::Sleep);
    }
    uart_.enableTxEmptyInterrupt();
  } else {
    releaseOrArmTransferComplete();
  }
  return {};
}

std::expected<void, Writable::Error>
SerialWritable::flushInterruptDriven(size_t count) {
  utils::Timeout timeout(FLUSH_TIMEOUT_MS);
  const size_t pending = txBuffer.size();
  const size_t target = (count >= pending) ? 0u : pending - count;
  // Waking the writer on every transmitted byte would cost a context switch
  // per byte, so let the ISR wake it once the buffer has drained to the
  // low-water mark: it then gets a batch of free slots to refill while the
  // hardware still has txLowWater_ bytes queued, so the TX line does not fall
  // idle in the meantime. A full flush (target 0) waits for the whole ring.
  const size_t threshold = std::min(target, txLowWater_);

  while (txBuffer.size() > target) {
    // Armed outside the critical section below: a notification that arrives
    // between arming and waiting only causes one extra loop iteration, which
    // the buffer-level check above absorbs.
    txSync_->armWait();
    {
      Interrupt::CriticalSection cs;
      if (txBuffer.size() <= target) {
        break;
      }
      txWaitThreshold_ = threshold;
      txWaitArmed_ = true;
      if (!txSleepLock_) {
        txSleepLock_.emplace(power::SleepMode::Sleep);
      }
      uart_.disableTxCompleteInterrupt();
      uart_.enableTxEmptyInterrupt();
    }
    txSync_->wait(std::min(static_cast<uint32_t>(timeout.getDurationLeft()),
                           TX_WAIT_SLICE_MS));
    // Checked after the wait, so an expired timeout does not cost another
    // arm-and-wait round with a zero timeout.
    if (timeout.hasExpired()) {
      break;
    }
  }

  {
    Interrupt::CriticalSection cs;
    txWaitArmed_ = false;
  }

  if (txBuffer.size() > target) {
    // The transmitter made no progress within the timeout (stopped clock,
    // stuck line): stop driving it and drop the sleep lock, as the polled
    // path does.
    Interrupt::CriticalSection cs;
    uart_.disableTxEmptyInterrupt();
    uart_.disableTxCompleteInterrupt();
    txSleepLock_.reset();
    return std::unexpected(CouldNotWrite);
  }
  if (txBuffer.empty()) {
    releaseOrArmTransferComplete();
  }
  return {};
}

std::expected<void, Writable::Error>
SerialWritable::flushUnlocked(size_t count, bool blocking) {
  if (blocking) {
    return flushInterruptDriven(count);
  }
  return flushPolled(count);
}

void SerialWritable::beginWriteSession() noexcept {
  if (canBlock()) {
    txSync_->lock();
  }
}

void SerialWritable::endWriteSession() noexcept {
  if (canBlock()) {
    txSync_->unlock();
  }
}

std::expected<void, Writable::Error> SerialWritable::flush() {
  return flushUnlocked(txBuffer.size(), canBlock());
}

std::expected<size_t, Writable::Error>
SerialWritable::writeUnlocked(std::span<const uint8_t> data, bool blocking) {
  for (size_t i = 0; i < data.size(); i++) {
    uint8_t c = data[i];
    if (txBuffer.full()) {
      auto res = flushUnlocked(1, blocking);
      if (!res) {
        return std::unexpected(res.error());
      }
    }
    if (!txBuffer.try_push(c)) {
      return std::unexpected(CouldNotWrite);
    }
  }
  if (!txBuffer.empty()) {
    Interrupt::CriticalSection cs;
    if (!txSleepLock_) {
      txSleepLock_.emplace(power::SleepMode::Sleep);
    }
    uart_.disableTxCompleteInterrupt();
    uart_.enableTxEmptyInterrupt();
  }
  return data.size();
}

std::expected<size_t, Writable::Error>
SerialWritable::write(std::span<const uint8_t> data) {
  return writeUnlocked(data, canBlock());
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

  if (txWaitArmed_ && txBuffer.size() <= txWaitThreshold_) {
    txWaitArmed_ = false;
    txSync_->notifyFromIsr();
  }
}
