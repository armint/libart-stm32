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
  bool notified{false};
  std::function<void(uint32_t timeoutMs)> onWait;

  [[nodiscard]] bool canBlock() const override { return blockingAllowed; }

  void lock() override {
    lockDepth++;
    maxLockDepth = std::max(maxLockDepth, lockDepth);
  }

  void unlock() override { lockDepth--; }

  void armWait() override {
    armCount++;
    notified = false;
  }

  bool wait(uint32_t timeoutMs) override {
    waitCount++;
    if (onWait) {
      onWait(timeoutMs);
    }
    const bool wasNotified = notified;
    notified = false;
    return wasNotified;
  }

  void notifyFromIsr() override {
    notifyCount++;
    notified = true;
  }
};

// Ring of 9 slots, so 8 usable bytes -- the real RingBuffer keeps one free.
constexpr size_t RING_SLOTS = 9;
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
    while (popped < maxBytes && !sync.notified) {
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
  // twice: a plain (non-recursive) mutex would deadlock.
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
    if (f.sync.notified) {
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
