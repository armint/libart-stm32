#pragma once

#include "PowerManager.h"
#include "SleepMode.h"
#include <utility>

namespace power {

/**
 * @brief RAII guard that holds a sleep constraint while in scope.
 */
class SleepLock {
public:
  explicit SleepLock(SleepMode maxAllowedMode)
      : mode_(maxAllowedMode), active_(true) {
    PowerManager::instance().acquire(mode_);
  }

  ~SleepLock() { release(); }

  SleepLock(const SleepLock &) = delete;
  SleepLock &operator=(const SleepLock &) = delete;

  SleepLock(SleepLock &&other) noexcept
      : mode_(other.mode_), active_(other.active_) {
    other.active_ = false;
  }

  SleepLock &operator=(SleepLock &&other) noexcept {
    if (this != &other) {
      release();
      mode_ = other.mode_;
      active_ = other.active_;
      other.active_ = false;
    }
    return *this;
  }

  void release() {
    if (active_) {
      PowerManager::instance().release(mode_);
      active_ = false;
    }
  }

  bool isActive() const { return active_; }

  SleepMode mode() const { return mode_; }

private:
  SleepMode mode_{SleepMode::Stop2};
  bool active_{false};
};

} // namespace power
