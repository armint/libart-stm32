#pragma once

#include <expected>
#include <optional>
#include <span>

#include <utils/RingBuffer.h>

#include "SerialTxSync.h"

#include <peripherals/Usart.h>
#include <power/SleepLock.h>
#include <streams/Writable.h>

using hw::Usart;

class SerialWritable : public Writable {
protected:
  Usart &uart_;
  RingBufferBase<uint8_t> &txBuffer;
  std::optional<power::SleepLock> txSleepLock_;
  SerialTxSync *txSync_;
  size_t txLowWater_;

  // Written by the writer with the USART interrupt masked, cleared by the ISR
  // before it notifies. Aligned word-sized accesses on Cortex-M are atomic, so
  // volatile is enough to keep the compiler from caching them.
  volatile size_t txWaitThreshold_{0};
  volatile bool txWaitArmed_{false};

  [[nodiscard]] bool canBlock() const {
    return txSync_ != nullptr && txSync_->canBlock();
  }

  std::expected<void, Error> flush(size_t count);
  std::expected<void, Error> flushUnlocked(size_t count, bool blocking);
  std::expected<void, Error> flushPolled(size_t count);
  std::expected<void, Error> flushInterruptDriven(size_t count);
  std::expected<size_t, Error> writeUnlocked(std::span<const uint8_t> data,
                                             bool blocking);
  void releaseOrArmTransferComplete();

public:
  /**
   * @param uart        the USART to transmit on.
   * @param txBuffer    software TX ring buffer drained by handleTxInterrupt().
   * @param txSync      optional synchronisation hooks. When supplied, write()
   *                    and flush() block on the TX interrupt instead of
   *                    polling TXE, in every context where
   *                    SerialTxSync::canBlock() allows it.
   * @param txLowWater  buffer level at which the ISR wakes a writer that is
   *                    waiting for free space; about half the ring capacity is
   *                    a sensible choice. 0 makes such a writer wait for the
   *                    ring to drain completely. Ignored without txSync.
   */
  SerialWritable(Usart &uart, RingBufferBase<uint8_t> &txBuffer,
                 SerialTxSync *txSync = nullptr, size_t txLowWater = 0);
  ~SerialWritable() override;

  std::expected<size_t, Error> write(std::span<const uint8_t> data) override;
  std::expected<void, Error> flush() override;

  // To be called from the device ISR
  void handleTxInterrupt();
};
