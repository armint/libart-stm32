#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "power/PowerManager.h"
#include "power/SleepLock.h"
#include <doctest/doctest.h>
#include <thread>
#include <vector>

using namespace power;

TEST_CASE("PowerManager initial state") {
  PowerManager &pm = PowerManager::instance();
  pm.reset();
  CHECK(pm.deepestAllowedMode() == SleepMode::Stop2);
  CHECK(pm.getLockCount(SleepMode::Run) == 0);
  CHECK(pm.getLockCount(SleepMode::Sleep) == 0);
  CHECK(pm.getLockCount(SleepMode::LPRun) == 0);
  CHECK(pm.getLockCount(SleepMode::Stop2) == 0);
}

TEST_CASE("PowerManager single acquire and release") {
  PowerManager &pm = PowerManager::instance();
  pm.reset();

  pm.acquire(SleepMode::Sleep);
  CHECK(pm.getLockCount(SleepMode::Sleep) == 1);
  CHECK(pm.deepestAllowedMode() == SleepMode::Sleep);

  pm.release(SleepMode::Sleep);
  CHECK(pm.getLockCount(SleepMode::Sleep) == 0);
  CHECK(pm.deepestAllowedMode() == SleepMode::Stop2);
}

TEST_CASE("PowerManager priority hierarchy (Run > Sleep > LPRun > Stop2)") {
  PowerManager &pm = PowerManager::instance();
  pm.reset();

  pm.acquire(SleepMode::LPRun);
  CHECK(pm.deepestAllowedMode() == SleepMode::LPRun);

  pm.acquire(SleepMode::Sleep);
  CHECK(pm.deepestAllowedMode() == SleepMode::Sleep);

  pm.acquire(SleepMode::Run);
  CHECK(pm.deepestAllowedMode() == SleepMode::Run);

  // Releasing Run falls back to Sleep
  pm.release(SleepMode::Run);
  CHECK(pm.deepestAllowedMode() == SleepMode::Sleep);

  // Releasing Sleep falls back to LPRun
  pm.release(SleepMode::Sleep);
  CHECK(pm.deepestAllowedMode() == SleepMode::LPRun);

  // Releasing LPRun falls back to Stop2
  pm.release(SleepMode::LPRun);
  CHECK(pm.deepestAllowedMode() == SleepMode::Stop2);
}

TEST_CASE("PowerManager reference counting per mode") {
  PowerManager &pm = PowerManager::instance();
  pm.reset();

  pm.acquire(SleepMode::Sleep);
  pm.acquire(SleepMode::Sleep);
  pm.acquire(SleepMode::Sleep);
  CHECK(pm.getLockCount(SleepMode::Sleep) == 3);
  CHECK(pm.deepestAllowedMode() == SleepMode::Sleep);

  pm.release(SleepMode::Sleep);
  CHECK(pm.getLockCount(SleepMode::Sleep) == 2);
  CHECK(pm.deepestAllowedMode() == SleepMode::Sleep);

  pm.release(SleepMode::Sleep);
  CHECK(pm.getLockCount(SleepMode::Sleep) == 1);
  CHECK(pm.deepestAllowedMode() == SleepMode::Sleep);

  pm.release(SleepMode::Sleep);
  CHECK(pm.getLockCount(SleepMode::Sleep) == 0);
  CHECK(pm.deepestAllowedMode() == SleepMode::Stop2);
}

TEST_CASE("PowerManager boundary behavior at 0 (no underflow)") {
  PowerManager &pm = PowerManager::instance();
  pm.reset();

  pm.release(SleepMode::Run);
  pm.release(SleepMode::Sleep);
  pm.release(SleepMode::LPRun);

  CHECK(pm.getLockCount(SleepMode::Run) == 0);
  CHECK(pm.getLockCount(SleepMode::Sleep) == 0);
  CHECK(pm.getLockCount(SleepMode::LPRun) == 0);
  CHECK(pm.deepestAllowedMode() == SleepMode::Stop2);
}

TEST_CASE("PowerManager Stop2 is no-op") {
  PowerManager &pm = PowerManager::instance();
  pm.reset();

  pm.acquire(SleepMode::Stop2);
  CHECK(pm.getLockCount(SleepMode::Stop2) == 0);
  CHECK(pm.deepestAllowedMode() == SleepMode::Stop2);

  pm.release(SleepMode::Stop2);
  CHECK(pm.getLockCount(SleepMode::Stop2) == 0);
  CHECK(pm.deepestAllowedMode() == SleepMode::Stop2);
}

TEST_CASE("PowerManager reset resets all counters") {
  PowerManager &pm = PowerManager::instance();
  pm.reset();

  pm.acquire(SleepMode::Run);
  pm.acquire(SleepMode::Sleep);
  pm.acquire(SleepMode::LPRun);

  CHECK(pm.deepestAllowedMode() == SleepMode::Run);
  pm.reset();
  CHECK(pm.deepestAllowedMode() == SleepMode::Stop2);
  CHECK(pm.getLockCount(SleepMode::Run) == 0);
  CHECK(pm.getLockCount(SleepMode::Sleep) == 0);
  CHECK(pm.getLockCount(SleepMode::LPRun) == 0);
}

TEST_CASE("SleepLock RAII acquire and release on scope exit") {
  PowerManager &pm = PowerManager::instance();
  pm.reset();

  {
    SleepLock lock(SleepMode::Sleep);
    CHECK(lock.isActive());
    CHECK(lock.mode() == SleepMode::Sleep);
    CHECK(pm.getLockCount(SleepMode::Sleep) == 1);
    CHECK(pm.deepestAllowedMode() == SleepMode::Sleep);
  }

  CHECK(pm.getLockCount(SleepMode::Sleep) == 0);
  CHECK(pm.deepestAllowedMode() == SleepMode::Stop2);
}

TEST_CASE("SleepLock manual release") {
  PowerManager &pm = PowerManager::instance();
  pm.reset();

  SleepLock lock(SleepMode::Run);
  CHECK(lock.isActive());
  CHECK(pm.deepestAllowedMode() == SleepMode::Run);

  lock.release();
  CHECK(!lock.isActive());
  CHECK(pm.deepestAllowedMode() == SleepMode::Stop2);

  // Second release is a no-op
  lock.release();
  CHECK(pm.deepestAllowedMode() == SleepMode::Stop2);
}

TEST_CASE("SleepLock move constructor and assignment") {
  PowerManager &pm = PowerManager::instance();
  pm.reset();

  {
    SleepLock lock1(SleepMode::Sleep);
    CHECK(pm.getLockCount(SleepMode::Sleep) == 1);

    SleepLock lock2(std::move(lock1));
    CHECK(!lock1.isActive());
    CHECK(lock2.isActive());
    CHECK(pm.getLockCount(SleepMode::Sleep) == 1);

    SleepLock lock3(SleepMode::LPRun);
    CHECK(pm.getLockCount(SleepMode::LPRun) == 1);

    lock3 = std::move(lock2); // Releases LPRun, takes ownership of Sleep
    CHECK(!lock2.isActive());
    CHECK(lock3.isActive());
    CHECK(pm.getLockCount(SleepMode::LPRun) == 0);
    CHECK(pm.getLockCount(SleepMode::Sleep) == 1);
  }

  CHECK(pm.getLockCount(SleepMode::Sleep) == 0);
  CHECK(pm.getLockCount(SleepMode::LPRun) == 0);
  CHECK(pm.deepestAllowedMode() == SleepMode::Stop2);
}

TEST_CASE("PowerManager concurrent acquire and release stress test") {
  PowerManager &pm = PowerManager::instance();
  pm.reset();

  constexpr int kThreads = 8;
  constexpr int kIterations = 10000;

  std::vector<std::thread> threads;
  threads.reserve(kThreads);

  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&pm, i]() {
      const SleepMode mode = (i % 2 == 0) ? SleepMode::Sleep : SleepMode::LPRun;
      for (int j = 0; j < kIterations; ++j) {
        pm.acquire(mode);
        pm.release(mode);
      }
    });
  }

  for (auto &t : threads) {
    t.join();
  }

  CHECK(pm.getLockCount(SleepMode::Run) == 0);
  CHECK(pm.getLockCount(SleepMode::Sleep) == 0);
  CHECK(pm.getLockCount(SleepMode::LPRun) == 0);
  CHECK(pm.deepestAllowedMode() == SleepMode::Stop2);
}
