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
  /**
   * @brief Callback invoked to bring the system clock back up after a sleep.
   *
   * @warning It is invoked after every Stop2 attempt, including attempts that
   * never actually slept. `__WFI()` returns immediately when an enabled
   * interrupt is already pending, in which case Stop2 was not entered and the
   * clock was never lost — and there is no architectural flag the hook can
   * test to tell the two apart. Implementations must therefore be
   * **idempotent and cheap**: check whether the clock is already at the
   * intended source and return early if so, rather than unconditionally
   * re-running a full bring-up.
   *
   * This matters on STM32WL in particular, where bringing up the 48 MHz
   * HSE/TCXO clock talks to the sub-GHz radio; an unconditional bring-up on
   * every idle pass can disturb radio state.
   */
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
   * execute immediately.
   *
   * Passed as a mandatory constructor argument (no default) precisely because
   * an omitted callback silently drops sleep-time compensation instead of
   * failing to build: under an RTOS, RTOS time will drift and threads
   * unblocked by the waking ISR won't be rescheduled promptly, with no
   * observable symptom until the drift accumulates. Bare-metal callers with
   * no RTOS to compensate must still pass an explicit no-op (e.g.
   * `[](uint32_t) {}`) so that choice is visible at the call site rather than
   * an accidental default.
   */
  using TickCompensationCallback = void (*)(uint32_t elapsedMs);

  /**
   * @brief Optional trace callback for Stop2 debug instrumentation.
   *
   * Called at each stage of enterStop2() with a single-character breadcrumb.
   * When output stops, the last character identifies the hang point.
   * Set via setTraceCallback(); nullptr (default) disables tracing.
   */
  using TraceCallback = void (*)(char stage);

  // tickCompCb has no default -- see TickCompensationCallback's doc comment
  // for why it must be supplied explicitly at every call site.
  explicit TicklessIdleHook(
      hw::Rtc &rtc, TickCompensationCallback tickCompCb,
      ClockRestoreCallback restoreClockCb = nullptr,
      uint32_t minStop2DurationMs = 3u,
      LPRunEnterCallback enterLPRunCb = nullptr)
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

  void setTraceCallback(TraceCallback cb) { traceCallback_ = cb; }

private:
  hw::Rtc &rtc_;
  ClockRestoreCallback restoreClockCallback_{nullptr};
  LPRunEnterCallback enterLPRunCallback_{nullptr};
  TickCompensationCallback tickCompensationCallback_{nullptr};
  TraceCallback traceCallback_{nullptr};
  uint32_t minStop2DurationMs_{3u};

  void enterSleep();
  void enterLPRun();
  void enterStop2(uint32_t durationMs);
};

} // namespace power
