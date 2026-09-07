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

  std::expected<void, Error> flushUnlocked(size_t count, bool blocking);
  std::expected<void, Error> flushPolled(size_t count);
  std::expected<void, Error> flushInterruptDriven(size_t count);
  std::expected<size_t, Error> writeUnlocked(std::span<const uint8_t> data,
                                             bool blocking);
  void releaseOrArmTransferComplete();

  // Take/release txSync_'s lock for a whole WriteSession instead of once per
  // write()/flush() call -- see beginWriteSession()/endWriteSession() below.
  // Each independently re-evaluates canBlock(), so a session's begin and its
  // matching end must observe the same answer; that holds as long as nothing
  // between them changes whether blocking is currently allowed (ISRs never
  // log here, and nothing on the logging path masks interrupts or suspends
  // the scheduler across a session's whole lifetime, only briefly inside
  // flush()'s own critical sections, which complete before the session
  // closes). Nesting itself is not tracked here: it is txSync_->lock()'s job
  // to tolerate the same task re-entering (see the warning on
  // SerialTxSync::lock()).
  void beginWriteSession() noexcept override;
  void endWriteSession() noexcept override;

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

  /**
   * @note write()/flush() no longer take txSync_'s lock themselves -- locking
   * now happens once per WriteSession, in beginWriteSession()/
   * endWriteSession() (see below), instead of once per call. Both are called
   * unconditionally on every path today (Writable::vprintf() and
   * logging::Logger::vlog() always open one), so this is transparent there.
   * A caller that invokes write()/flush() directly, outside any writeSession()
   * -- e.g. a raw, unformatted write -- is therefore no longer serialised
   * against a concurrent writer on the same sink by this class; wrap such a
   * call in writeSession() if txSync_ is set and concurrent access is
   * possible.
   */
  std::expected<size_t, Error> write(std::span<const uint8_t> data) override;
  std::expected<void, Error> flush() override;

  // To be called from the device ISR
  void handleTxInterrupt();
};
