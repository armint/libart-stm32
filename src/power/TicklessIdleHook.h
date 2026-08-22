#pragma once

#include "PowerManager.h"
#include "SleepMode.h"
#include <cstdint>
#include <peripherals/Rtc.h>

namespace power {

/**
 * @brief RTOS-agnostic tickless idle coordinator.
 *
 * Evaluates active power locks on every idle hook pass, configuring and
 * entering the appropriate sleep mode (Run, Sleep, LPRun, Stop2).
 */
class TicklessIdleHook {
public:
  using ClockRestoreCallback = void (*)();
  using LPRunEnterCallback = void (*)();
  /**
   * @brief Callback invoked after waking from Stop 2 and restoring SysTick.
   *
   * @param elapsedMs Elapsed time in milliseconds spent in Stop 2. Can be 0 if
   * the sleep duration was sub-millisecond or crossed a 24-hour midnight wrap.
   *
   * @note The callback is called unconditionally after SysTick is resumed.
   * Under an RTOS, the implementation must advance the RTOS time (if elapsedMs > 0)
   * and request a scheduler reschedule so threads unblocked by the waking ISR
   * execute immediately. If unset, sleep time in Stop 2 will not be accounted
   * for and RTOS time will drift.
   */
  using TickCompensationCallback = void (*)(uint32_t elapsedMs);

  explicit TicklessIdleHook(
      hw::Rtc &rtc, ClockRestoreCallback restoreClockCb = nullptr,
      uint32_t minStop2DurationMs = 3u,
      LPRunEnterCallback enterLPRunCb = nullptr,
      TickCompensationCallback tickCompCb = nullptr)
      : rtc_(rtc), restoreClockCallback_(restoreClockCb),
        enterLPRunCallback_(enterLPRunCb),
        tickCompensationCallback_(tickCompCb),
        minStop2DurationMs_(minStop2DurationMs) {}

  /**
   * @brief Handles idle processing according to allowed sleep mode and expected
   * idle duration.
   *
   * @param expectedIdleMs Expected idle duration in milliseconds until the next
   * scheduled deadline. Pass `UINT32_MAX` (or any large value) if no deadline is
   * scheduled; values are automatically clamped to the maximum safe wakeup duration.
   *
   * @return true when idle hook handled the power saving mode.
   */
  bool handleIdle(uint32_t expectedIdleMs);

  void setMinStop2DurationMs(uint32_t minMs) { minStop2DurationMs_ = minMs; }

  uint32_t minStop2DurationMs() const { return minStop2DurationMs_; }

  void setClockRestoreCallback(ClockRestoreCallback cb) {
    restoreClockCallback_ = cb;
  }

  void setLPRunEnterCallback(LPRunEnterCallback cb) {
    enterLPRunCallback_ = cb;
  }

  void setTickCompensationCallback(TickCompensationCallback cb) {
    tickCompensationCallback_ = cb;
  }

private:
  hw::Rtc &rtc_;
  ClockRestoreCallback restoreClockCallback_{nullptr};
  LPRunEnterCallback enterLPRunCallback_{nullptr};
  TickCompensationCallback tickCompensationCallback_{nullptr};
  uint32_t minStop2DurationMs_{3u};

  void enterSleep();
  void enterLPRun();
  void enterStop2(uint32_t durationMs);
};

} // namespace power
