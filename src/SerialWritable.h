#pragma once

#include <utils/compat/expected.h>
#include <optional>
#include <utils/compat/span.h>

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

  // Whether write()/flush() should take txSync_'s lock themselves right now:
  // blocking must be allowed, and the current task must not already hold it
  // via an enclosing WriteSession (see SerialTxSync::heldByCaller()).
  // heldByCaller() is a genuine per-task query (e.g. backed by
  // xSemaphoreGetMutexHolder() == xTaskGetCurrentTaskHandle()), so this is
  // safe to evaluate fresh on every write()/flush() call: a call from a
  // different task correctly sees "not held by me" and locks for real, which
  // correctly blocks against whichever task actually holds it.
  //
  // beginWriteSession()/endWriteSession() below deliberately do NOT use this
  // check, nor track anything like "is a session already open" themselves --
  // that would be per-instance state answering a per-task question. A task
  // can hold a session open across a blocking wait() (see
  // flushInterruptDriven()), during which a genuinely different task must
  // still see "not held" and really lock; instance-wide bookkeeping cannot
  // tell that apart from "the same task re-entering" without also racing on
  // the bookkeeping itself at the lock/unlock boundary. So every
  // beginWriteSession()/endWriteSession() call locks/unlocks unconditionally
  // on canBlock(), same as write()/flush() did before this class opted into
  // WriteSession: two lock/unlock pairs for a nested Logger-plus-printf line
  // rather than one, but correct under real cross-task contention, which a
  // single shared pair is not.
  [[nodiscard]] bool shouldSelfLock() const {
    return canBlock() && !txSync_->heldByCaller();
  }

  art::expected<void, Error> flushUnlocked(size_t count, bool blocking);
  art::expected<void, Error> flushPolled(size_t count);
  art::expected<void, Error> flushInterruptDriven(size_t count);
  art::expected<size_t, Error> writeUnlocked(art::span<const uint8_t> data,
                                             bool blocking);
  void releaseOrArmTransferComplete();

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

  art::expected<size_t, Error> write(art::span<const uint8_t> data) override;
  art::expected<void, Error> flush() override;

  // To be called from the device ISR
  void handleTxInterrupt();
};
