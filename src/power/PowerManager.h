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
 */
class PowerManager {
public:
  static PowerManager &instance();

  /**
   * @brief Acquires a constraint that prevents entering any sleep mode deeper
   * than maxAllowedMode.
   */
  void acquire(SleepMode maxAllowedMode);

  /**
   * @brief Releases a previously acquired constraint.
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
   */
  uint32_t getLockCount(SleepMode mode) const;

private:
  PowerManager() = default;
  ~PowerManager() = default;

  PowerManager(const PowerManager &) = delete;
  PowerManager &operator=(const PowerManager &) = delete;
  PowerManager(PowerManager &&) = delete;
  PowerManager &operator=(PowerManager &&) = delete;

  // Index 0: Run, Index 1: Sleep, Index 2: LPRun
  std::array<uint32_t, 3> lockCounts_{};
};

} // namespace power
