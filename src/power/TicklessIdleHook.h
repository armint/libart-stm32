#pragma once

#include "PowerManager.h"
#include "SleepMode.h"
#include <cmsis-plus/rtos/os.h>
#include <cstdint>
#include <peripherals/Rtc.h>

namespace power {

/**
 * @brief Generic µOS++ IIIe tickless idle coordinator.
 *
 * Evaluates active power locks and scheduled RTOS deadlines on every idle
 * thread entry, configuring and entering the appropriate sleep mode (Run,
 * Sleep, LPRun, Stop2).
 */
class TicklessIdleHook {
public:
  using ClockRestoreCallback = void (*)();
  using LPRunEnterCallback = void (*)();

  explicit TicklessIdleHook(hw::Rtc &rtc,
                            ClockRestoreCallback restoreClockCb = nullptr,
                            uint32_t minStop2DurationMs = 3u,
                            LPRunEnterCallback enterLPRunCb = nullptr)
      : rtc_(rtc), restoreClockCallback_(restoreClockCb),
        enterLPRunCallback_(enterLPRunCb),
        minStop2DurationMs_(minStop2DurationMs) {}

  /**
   * @brief Handles idle processing according to allowed sleep mode and next
   * deadline.
   * @return true when idle hook handled the power saving mode.
   */
  bool handleIdle();

  /**
   * @brief Computes the duration until the next RTOS deadline in milliseconds.
   * @return Milliseconds until next deadline, or UINT32_MAX if no deadline is
   * scheduled.
   */
  static uint32_t computeTimeToNextDeadlineMs();

  void setMinStop2DurationMs(uint32_t minMs) { minStop2DurationMs_ = minMs; }

  uint32_t minStop2DurationMs() const { return minStop2DurationMs_; }

  void setClockRestoreCallback(ClockRestoreCallback cb) {
    restoreClockCallback_ = cb;
  }

  void setLPRunEnterCallback(LPRunEnterCallback cb) {
    enterLPRunCallback_ = cb;
  }

private:
  hw::Rtc &rtc_;
  ClockRestoreCallback restoreClockCallback_{nullptr};
  LPRunEnterCallback enterLPRunCallback_{nullptr};
  uint32_t minStop2DurationMs_{3u};

  void enterSleep();
  void enterLPRun();
  void enterStop2(uint32_t durationMs);
};

} // namespace power
