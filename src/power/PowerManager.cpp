#include "PowerManager.h"
#include <cmsis-plus/rtos/os.h>

namespace power {

PowerManager::PowerManager() = default;

namespace {
// Eager namespace-scope static instance: initialized at startup before main(),
// avoiding runtime Meyers-singleton initialization locks (__cxa_guard_acquire)
// so that PowerManager::instance() is always lock-free and safe to call from
// ISRs.
PowerManager globalPowerManager;
} // namespace

PowerManager &PowerManager::instance() { return globalPowerManager; }

void PowerManager::acquire(SleepMode maxAllowedMode) {
  if (maxAllowedMode >= SleepMode::Stop2) {
    return;
  }
  os::rtos::interrupts::critical_section cs;
  ++lockCounts_[static_cast<size_t>(maxAllowedMode)];
}

void PowerManager::release(SleepMode maxAllowedMode) {
  if (maxAllowedMode >= SleepMode::Stop2) {
    return;
  }
  os::rtos::interrupts::critical_section cs;
  auto &count = lockCounts_[static_cast<size_t>(maxAllowedMode)];
  if (count > 0u) {
    --count;
  }
}

SleepMode PowerManager::deepestAllowedMode() const {
  os::rtos::interrupts::critical_section cs;
  if (lockCounts_[static_cast<size_t>(SleepMode::Run)] > 0u) {
    return SleepMode::Run;
  }
  if (lockCounts_[static_cast<size_t>(SleepMode::Sleep)] > 0u) {
    return SleepMode::Sleep;
  }
  if (lockCounts_[static_cast<size_t>(SleepMode::LPRun)] > 0u) {
    return SleepMode::LPRun;
  }
  return SleepMode::Stop2;
}

void PowerManager::reset() {
  os::rtos::interrupts::critical_section cs;
  lockCounts_.fill(0u);
}

uint32_t PowerManager::getLockCount(SleepMode mode) const {
  if (mode >= SleepMode::Stop2) {
    return 0u;
  }
  os::rtos::interrupts::critical_section cs;
  return lockCounts_[static_cast<size_t>(mode)];
}

} // namespace power
