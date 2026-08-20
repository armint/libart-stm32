#pragma once

#include "SleepMode.h"
#include <array>
#include <cmsis-plus/rtos/os.h>
#include <cstddef>
#include <cstdint>

namespace power {

/**
 * @brief Thread- and ISR-safe reference counter and deepest allowable sleep
 * mode solver.
 *
 * Backed by an eager static instance, making PowerManager::instance() lock-free
 * and safe to call from both thread and ISR contexts without runtime guard
 * initialization.
 */
class PowerManager {
public:
  /**
   * @brief Access the global PowerManager instance. Safe to call from ISR
   * context.
   */
  static PowerManager &instance();

  /**
   * @brief Acquires a constraint that prevents entering any sleep mode deeper
   * than maxAllowedMode. Safe to call from thread and ISR contexts.
   */
  void acquire(SleepMode maxAllowedMode);

  /**
   * @brief Releases a previously acquired constraint. Safe to call from thread
   * and ISR contexts.
   */
  void release(SleepMode maxAllowedMode);

  /**
   * @brief Determines the deepest allowed sleep mode permitted by all active
   * locks.
   * @return SleepMode (Run, Sleep, LPRun, or Stop2 if no restrictive locks are
   * held).
   */
  SleepMode deepestAllowedMode() const;

  /**
   * @brief Resets all lock counters to zero.
   */
  void reset();

  /**
   * @brief Returns the active lock count for a specific mode.
   * @note Returns 0 for SleepMode::Stop2 because Stop2 is the unconstrained
   * baseline and is not tracked.
   */
  uint32_t getLockCount(SleepMode mode) const;

  PowerManager();
  ~PowerManager() = default;

  PowerManager(const PowerManager &) = delete;
  PowerManager &operator=(const PowerManager &) = delete;
  PowerManager(PowerManager &&) = delete;
  PowerManager &operator=(PowerManager &&) = delete;

private:
  // Index 0: Run, Index 1: Sleep, Index 2: LPRun
  std::array<uint32_t, 3> lockCounts_{};

  static_assert(sizeof(lockCounts_) / sizeof(lockCounts_[0]) ==
                    static_cast<size_t>(SleepMode::Stop2),
                "lockCounts_ array size must match number of constrained modes "
                "below Stop2");
};

} // namespace power
