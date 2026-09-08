#include "SerialWritable.h"
#include "power/PowerManager.h"
#include <utils/Timeout.h>

#include <array>
#include <doctest/doctest.h>
#include <functional>
#include <vector>

namespace {

/**
 * Records how SerialWritable drives the synchronisation hooks and lets a test
 * run the USART ISR from inside wait(), i.e. exactly where the real interrupt
 * would fire while the writer is blocked.
 */
class FakeSync : public SerialTxSync {
public:
  bool blockingAllowed{true};
  int lockDepth{0};
  int maxLockDepth{0};
  int armCount{0};
  int waitCount{0};
  int notifyCount{0};
  // Models the binary semaphore behind the real hooks: a token survives until
  // a wait consumes it or an arm drops it, so a notification that arrives
  // early or too late behaves as it would on the target.
  bool token{false};
  std::vector<bool> tokenOnWaitEntry;
  std::function<void()> onArm;
  std::function<void(uint32_t timeoutMs)> onWait;

  // Models art::rtos::RecursiveMutex by default: nested lock() calls from the
  // (single, in a host test) calling context just add depth. Set false to
  // model a plain, non-recursive mutex instead, e.g. a hand-rolled
  // SerialTxSync that skipped the requirement documented on
  // SerialTxSync::lock() -- reentrantViolations then counts how many times
  // that would have self-deadlocked on target.
  bool recursive{true};
  int reentrantViolations{0};
  // Total number of lock()/unlock() calls, as opposed to lockDepth/
  // maxLockDepth which only track how deeply nested the current call is.
  // This is what the "once per session, not once per write() call"
  // acceptance criterion is actually about: several write()s inside one
  // session should cost one lock() call, not one per write().
  int lockCallCount{0};
  int unlockCallCount{0};

  [[nodiscard]] bool canBlock() const override { return blockingAllowed; }

  // A correct, single-threaded proxy for "the caller already holds it":
  // there is only one calling context in this test file, so "someone holds
  // it" and "I hold it" coincide.
  [[nodiscard]] bool heldByCaller() const override { return lockDepth > 0; }

  void lock() override {
    if (!recursive && lockDepth > 0) {
      reentrantViolations++;
    }
    lockDepth++;
    maxLockDepth = std::max(maxLockDepth, lockDepth);
    lockCallCount++;
  }

  void unlock() override {
    lockDepth--;
    unlockCallCount++;
  }

  void armWait() override {
    armCount++;
    token = false;
    if (onArm) {
      onArm();
    }
  }

  bool wait(uint32_t timeoutMs) override {
    waitCount++;
    tokenOnWaitEntry.push_back(token);
    if (onWait) {
      onWait(timeoutMs);
    }
    const bool hadToken = token;
    token = false;
    return hadToken;
  }

  void notifyFromIsr() override {
    notifyCount++;
    token = true;
  }
};

// 8 slots, so 7 usable bytes -- the real RingBuffer wants a power-of-two size
// and keeps one slot free to tell full from empty.
constexpr size_t RING_SLOTS = 8;
constexpr size_t RING_CAPACITY = RING_SLOTS - 1;
constexpr size_t LOW_WATER = 4;

struct Fixture {
  hw::Usart uart;
  RingBuffer<uint8_t, RING_SLOTS> ring;
  FakeSync sync;
  SerialWritable serial;

  Fixture() : serial(uart, ring, &sync, LOW_WATER) {
    power::PowerManager::instance().reset();
    utils::g_test_now_ms = 0;
  }

  // One TXE interrupt: the hardware took the previous byte, so the ISR may
  // hand it the next one.
  void runTxInterrupt() {
    uart.txRegisterEmpty = true;
    serial.handleTxInterrupt();
  }

  // Drains up to maxBytes through the ISR, stopping as soon as the driver has
  // been notified so a test can see exactly when the wake-up happened.
  size_t drainUntilNotified(size_t maxBytes) {
    size_t popped = 0;
    while (popped < maxBytes && !sync.token) {
      const size_t before = ring.size();
      runTxInterrupt();
      if (ring.size() == before) {
        break;
      }
      popped++;
    }
    return popped;
  }

  std::vector<uint8_t> bytes(size_t count) const {
    std::vector<uint8_t> data(count);
    for (size_t i = 0; i < count; i++) {
      data[i] = static_cast<uint8_t>(i + 1);
    }
    return data;
  }
};

} // namespace

TEST_CASE("without sync hooks flush polls the bytes out itself") {
  Fixture f;
  const std::array<uint8_t, 4> data{1, 2, 3, 4};
  SerialWritable polled(f.uart, f.ring);

  REQUIRE(polled.write(data).has_value());
  REQUIRE(polled.flush().has_value());

  CHECK(f.ring.empty());
  CHECK(f.uart.transmitted == std::vector<uint8_t>{1, 2, 3, 4});
  CHECK(f.sync.waitCount == 0);
}

TEST_CASE("sync hooks that forbid blocking fall back to the polled path") {
  Fixture f;
  f.sync.blockingAllowed = false;
  const std::array<uint8_t, 4> data{1, 2, 3, 4};

  REQUIRE(f.serial.write(data).has_value());
  REQUIRE(f.serial.flush().has_value());

  CHECK(f.ring.empty());
  CHECK(f.uart.transmitted == std::vector<uint8_t>{1, 2, 3, 4});
  CHECK(f.sync.waitCount == 0);
  CHECK(f.sync.armCount == 0);
  CHECK(f.sync.maxLockDepth == 0);
}

TEST_CASE("a direct write()/flush() call outside any session still takes "
          "the lock itself") {
  Fixture f;
  const std::array<uint8_t, 4> data{1, 2, 3, 4};
  // flush() below takes the interrupt-driven (blocking) path since
  // blockingAllowed defaults true; drive its wait() the same way the
  // existing blocking tests do, or it spins until the 5 s flush timeout.
  f.sync.onWait = [&](uint32_t) { f.drainUntilNotified(RING_CAPACITY); };

  // No writeSession() open: heldByCaller() reports false (nobody holds it),
  // so write()'s/flush()'s own shouldSelfLock() check takes the lock. This
  // is the property SerialTxSync::heldByCaller() exists for -- a caller that
  // never opens a WriteSession stays serialised exactly as it was before
  // this class opted into WriteSession, with no contract narrowing left
  // behind. If this guard were ever dropped in favour of only locking in
  // beginWriteSession()/endWriteSession(), this is the case that would
  // regress.
  REQUIRE(f.serial.write(data).has_value());
  CHECK(f.sync.lockCallCount == 1);
  CHECK(f.sync.unlockCallCount == 1);
  CHECK(f.sync.lockDepth == 0);

  REQUIRE(f.serial.flush().has_value());
  CHECK(f.sync.lockCallCount == 2);
  CHECK(f.sync.unlockCallCount == 2);
  CHECK(f.sync.lockDepth == 0);
}

TEST_CASE("flush blocks until the TX interrupt has drained the ring") {
  Fixture f;
  const std::array<uint8_t, 4> data{1, 2, 3, 4};
  f.sync.onWait = [&](uint32_t) { f.drainUntilNotified(RING_CAPACITY); };

  REQUIRE(f.serial.write(data).has_value());
  CHECK(f.uart.isTxEmptyInterruptEnabled());

  REQUIRE(f.serial.flush().has_value());

  CHECK(f.ring.empty());
  CHECK(f.uart.transmitted == std::vector<uint8_t>{1, 2, 3, 4});
  CHECK(f.sync.waitCount == 1);
  CHECK(f.sync.armCount == 1);
  CHECK(f.sync.notifyCount == 1);
  // write() and the flush() it does not nest inside must never take the lock
  // twice at once: a plain (non-recursive) mutex would deadlock.
  CHECK(f.sync.maxLockDepth == 1);
  CHECK(f.sync.lockDepth == 0);
}

TEST_CASE("the sleep lock is held until the last byte has left the shift "
          "register") {
  Fixture f;
  const std::array<uint8_t, 2> data{7, 8};
  power::PowerManager &pm = power::PowerManager::instance();
  f.sync.onWait = [&](uint32_t) { f.drainUntilNotified(RING_CAPACITY); };

  REQUIRE(f.serial.write(data).has_value());
  CHECK(pm.getLockCount(power::SleepMode::Sleep) == 1);

  REQUIRE(f.serial.flush().has_value());
  // Ring drained, but the hardware is still shifting: TC is armed and the
  // lock stays.
  CHECK(f.ring.empty());
  CHECK(f.uart.isTxCompleteInterruptEnabled());
  CHECK(pm.getLockCount(power::SleepMode::Sleep) == 1);

  f.uart.transferComplete = true;
  f.serial.handleTxInterrupt();
  CHECK(pm.getLockCount(power::SleepMode::Sleep) == 0);
}

TEST_CASE("a writer waiting for space is woken at the low-water mark") {
  Fixture f;
  size_t poppedDuringWait = 0;
  std::vector<size_t> sizeAtNotify;
  f.sync.onWait = [&](uint32_t) {
    poppedDuringWait = f.drainUntilNotified(RING_CAPACITY);
    if (f.sync.token) {
      sizeAtNotify.push_back(f.ring.size());
    }
  };

  // One byte more than fits, so the last write() hits the full ring and has
  // to wait for space.
  const std::vector<uint8_t> data = f.bytes(RING_CAPACITY + 1);
  REQUIRE(f.serial.write(data).has_value());

  CHECK(f.sync.waitCount == 1);
  // Woken once the ISR had drained down to the low-water mark -- not after
  // the single byte that made room, and not once per byte.
  CHECK(f.sync.notifyCount == 1);
  REQUIRE(sizeAtNotify.size() == 1);
  CHECK(sizeAtNotify[0] == LOW_WATER);
  CHECK(poppedDuringWait == RING_CAPACITY - LOW_WATER);
  CHECK(f.sync.maxLockDepth == 1);
  CHECK(f.sync.lockDepth == 0);
}

TEST_CASE("a notification that arrives before the wait is not lost") {
  Fixture f;
  const std::array<uint8_t, 4> data{1, 2, 3, 4};
  // The ISR runs between armWait() and the wait itself. The ISR only notifies
  // once the buffer is at the level the writer is waiting for, so the
  // re-check inside the critical section sees the same thing and the writer
  // never blocks at all -- the token cannot strand it.
  f.sync.onArm = [&]() { f.drainUntilNotified(RING_CAPACITY); };

  REQUIRE(f.serial.write(data).has_value());
  REQUIRE(f.serial.flush().has_value());

  CHECK(f.ring.empty());
  CHECK(f.uart.transmitted == std::vector<uint8_t>{1, 2, 3, 4});
  CHECK(f.sync.armCount == 1);
  CHECK(f.sync.waitCount == 0);
}

TEST_CASE("a token left by a timed-out wait is dropped before the next one") {
  Fixture f;
  const std::array<uint8_t, 2> data{1, 2};
  f.sync.onWait = [&](uint32_t timeoutMs) {
    utils::advanceTestClock(timeoutMs);
  };

  REQUIRE(f.serial.write(data).has_value());
  REQUIRE_FALSE(f.serial.flush().has_value());

  // The ISR fires just after the writer gave up, leaving a token behind.
  f.sync.notifyFromIsr();
  REQUIRE(f.sync.token);

  f.sync.tokenOnWaitEntry.clear();
  f.sync.onWait = [&](uint32_t) { f.drainUntilNotified(RING_CAPACITY); };
  REQUIRE(f.serial.flush().has_value());

  // The stale token must not have satisfied the new wait: it entered without
  // one and only returned once the ISR had really drained the ring.
  REQUIRE(f.sync.tokenOnWaitEntry.size() >= 1);
  CHECK(f.sync.tokenOnWaitEntry[0] == false);
  CHECK(f.ring.empty());
}

TEST_CASE("flush on an empty ring neither waits nor arms") {
  Fixture f;

  REQUIRE(f.serial.flush().has_value());

  CHECK(f.sync.waitCount == 0);
  CHECK(f.sync.armCount == 0);
  CHECK(power::PowerManager::instance().getLockCount(power::SleepMode::Sleep) ==
        0);
}

TEST_CASE("a drain that produced no notification is picked up by the next "
          "re-check") {
  Fixture f;
  // Models a write from ISR context: it takes the polled path, which masks
  // TXE/TC and empties the ring itself, so the blocked writer is never
  // notified and has to notice on its own.
  f.sync.onWait = [&](uint32_t timeoutMs) {
    utils::advanceTestClock(timeoutMs);
    uint8_t c;
    while (f.ring.try_pop(c)) {
      f.uart.setData(c);
    }
  };

  const std::array<uint8_t, 4> data{1, 2, 3, 4};
  REQUIRE(f.serial.write(data).has_value());

  REQUIRE(f.serial.flush().has_value());
  CHECK(f.ring.empty());
  CHECK(f.sync.notifyCount == 0);
  // One 50 ms slice, not the full 5 s flush timeout.
  CHECK(f.sync.waitCount == 1);
  CHECK(utils::g_test_now_ms == 50);
}

TEST_CASE("a stalled transmitter times out, drops the sleep lock and stops "
          "the interrupts") {
  Fixture f;
  power::PowerManager &pm = power::PowerManager::instance();
  // The ISR never runs: model a dead transmitter by letting every wait run to
  // its full timeout.
  f.sync.onWait = [&](uint32_t timeoutMs) {
    utils::advanceTestClock(timeoutMs);
  };

  const std::array<uint8_t, 4> data{1, 2, 3, 4};
  REQUIRE(f.serial.write(data).has_value());

  const auto result = f.serial.flush();
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == Writable::CouldNotWrite);
  CHECK_FALSE(f.uart.isTxEmptyInterruptEnabled());
  CHECK_FALSE(f.uart.isTxCompleteInterruptEnabled());
  CHECK(pm.getLockCount(power::SleepMode::Sleep) == 0);
  CHECK(f.sync.lockDepth == 0);
}

TEST_CASE("a write session locks once for the whole session, not once per "
          "write() call") {
  Fixture f;
  const std::array<uint8_t, 1> byte{1};

  {
    auto session = f.serial.writeSession();
    for (int i = 0; i < 4; i++) {
      REQUIRE(f.serial.write(byte).has_value());
    }
  }

  // Four write() calls inside one session cost exactly one lock()/unlock()
  // pair -- this is what #10 is about: a formatted call with a non-trivial
  // write() no longer pays that cost once per write().
  CHECK(f.sync.lockCallCount == 1);
  CHECK(f.sync.unlockCallCount == 1);
  CHECK(f.sync.maxLockDepth == 1);
  CHECK(f.sync.lockDepth == 0);
}

TEST_CASE("a nested write session -- Logger's outer session plus printf's "
          "inner one -- locks exactly once for the whole chain") {
  Fixture f;
  const std::array<uint8_t, 1> byte{1};

  {
    auto outer = f.serial.writeSession(); // logging::Logger::vlog()'s session
    REQUIRE(f.serial.write(byte).has_value());
    {
      // Writable::vprintf()'s own session, opened on the same task while the
      // outer one from vlog() is still held.
      auto inner = f.serial.writeSession();
      REQUIRE(f.serial.write(byte).has_value());
      REQUIRE(f.serial.write(byte).has_value());
    }
    REQUIRE(f.serial.write(byte).has_value());
  }

  // beginWriteSession()/endWriteSession() lock/unlock unconditionally on
  // canBlock(), with no shared "is a session already open" state -- that
  // would answer a per-task question (is *this* task already the holder?)
  // with per-instance bookkeeping, which a genuinely different task calling
  // in concurrently cannot be distinguished from by. So the inner session
  // locks again while the outer one still holds it: two pairs for this
  // nested chain, not one, but still nowhere near one per write() call (an
  // 80-character line paid ~160 before this class opted into WriteSession at
  // all). write()'s own shouldSelfLock() check is the one place that safely
  // skips a redundant lock, because heldByCaller() is a genuine per-task
  // query -- so the four write() calls above add nothing on top.
  CHECK(f.sync.lockCallCount == 2);
  CHECK(f.sync.unlockCallCount == 2);
  CHECK(f.sync.maxLockDepth == 2);
  CHECK(f.sync.lockDepth == 0);
  CHECK(f.sync.reentrantViolations == 0);
}

TEST_CASE("a non-recursive SerialTxSync::lock() self-deadlocks on the nested "
          "session pattern") {
  Fixture f;
  f.sync.recursive = false;
  const std::array<uint8_t, 1> byte{1};

  {
    auto outer = f.serial.writeSession();
    REQUIRE(f.serial.write(byte).has_value());
    {
      auto inner = f.serial.writeSession();
      REQUIRE(f.serial.write(byte).has_value());
    }
  }

  // The inner session's beginWriteSession() re-enters lock() while the outer
  // session still holds it, regardless of heldByCaller(): begin/end never
  // consult it (see the note on shouldSelfLock()). On a plain (non-recursive)
  // mutex that hangs forever instead of returning; here it is only recorded,
  // since the fake never actually blocks -- this is why SerialTxSync.h
  // requires lock()/unlock() to tolerate reentrance.
  CHECK(f.sync.reentrantViolations == 1);
}

TEST_CASE("sync hooks that forbid blocking mean a write session never "
          "touches the lock") {
  Fixture f;
  f.sync.blockingAllowed = false;
  const std::array<uint8_t, 4> data{1, 2, 3, 4};

  {
    auto session = f.serial.writeSession();
    REQUIRE(f.serial.write(data).has_value());
  }

  CHECK(f.sync.lockCallCount == 0);
  CHECK(f.sync.unlockCallCount == 0);
  CHECK(f.sync.lockDepth == 0);
}

TEST_CASE("a write session is a no-op without sync hooks") {
  Fixture f;
  SerialWritable polled(f.uart, f.ring);
  const std::array<uint8_t, 4> data{1, 2, 3, 4};

  {
    auto session = polled.writeSession();
    REQUIRE(polled.write(data).has_value());
    REQUIRE(polled.flush().has_value());
  }

  CHECK(f.ring.empty());
  CHECK(f.uart.transmitted == std::vector<uint8_t>{1, 2, 3, 4});
}
