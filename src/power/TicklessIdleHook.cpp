#include "TicklessIdleHook.h"
#include <cmsis_device.h>
#include <peripherals/Pwr.h>
#include <peripherals/SysTick.h>

namespace power {

uint32_t TicklessIdleHook::computeTimeToNextDeadlineMs() {
  os::rtos::interrupts::critical_section cs;
  const auto *head = os::rtos::sysclock.steady_list().head();
  if (head == nullptr) {
    return UINT32_MAX;
  }

  const auto now = os::rtos::sysclock.now();
  const auto deadline = head->timestamp;

  if (deadline <= now) {
    return 0u;
  }

  const auto diffTicks = deadline - now;
  constexpr uint32_t freqHz = os::rtos::clock_systick::frequency_hz;
  if constexpr (freqHz == 1000u) {
    return (diffTicks > UINT32_MAX) ? UINT32_MAX
                                    : static_cast<uint32_t>(diffTicks);
  } else {
    const uint64_t ms = (static_cast<uint64_t>(diffTicks) * 1000u) / freqHz;
    return (ms > UINT32_MAX) ? UINT32_MAX : static_cast<uint32_t>(ms);
  }
}

void TicklessIdleHook::enterSleep() {
  __DSB();
  __WFI();
  __ISB();
}

void TicklessIdleHook::enterLPRun() {
  if (enterLPRunCallback_) {
    enterLPRunCallback_();
  }
  __DSB();
  __WFI();
  __ISB();
  if (restoreClockCallback_) {
    restoreClockCallback_();
  }
}

void TicklessIdleHook::enterStop2(uint32_t durationMs) {
  // 1. Arm RTC Wakeup Timer
  rtc_.armWakeup(durationMs);

  // 2. Read timestamp immediately prior to entering Stop 2
  const uint64_t tsBefore = rtc_.getSubSecondTimestamp();

  // 3. Suspend SysTick
  hw::SysTick::disable();

  // 4. Configure Cortex-M deep sleep and PWR Stop 2 mode
  hw::Pwr::enableDeepSleep();
  hw::Pwr::setStopMode(hw::Pwr::StopMode::Stop2);

  // 5. Enter Stop 2
  __DSB();
  __WFI();
  __ISB();

  // 6. Wakeup path: Clear SLEEPDEEP
  hw::Pwr::disableDeepSleep();

  // 7. Restore high-speed system clocks immediately
  if (restoreClockCallback_) {
    restoreClockCallback_();
  }

  // 8. Measure elapsed time across sleep (works for RTC WUT, Radio DIO1, or any
  // EXTI)
  const uint64_t tsAfter = rtc_.getSubSecondTimestamp();
  const uint64_t elapsedMs = (tsAfter >= tsBefore) ? (tsAfter - tsBefore) : 0u;

  // 9. Disarm Wakeup Timer
  rtc_.disarmWakeup();

  // 10. Update RTOS steady clock for the slept duration
  if (elapsedMs > 0u) {
    os::rtos::sysclock.update_for_slept_time(
        static_cast<os::rtos::port::clock::duration_t>(elapsedMs));
  }

  // 11. Resume SysTick
  hw::SysTick::reset();
  hw::SysTick::enable();

  // 12. Trigger scheduler reschedule
  os::rtos::port::scheduler::reschedule();
}

bool TicklessIdleHook::handleIdle() {
  const SleepMode mode = PowerManager::instance().deepestAllowedMode();

  switch (mode) {
  case SleepMode::Run:
    // No sleep allowed, yield execution
    return true;

  case SleepMode::Sleep:
    enterSleep();
    return true;

  case SleepMode::LPRun:
    enterLPRun();
    return true;

  case SleepMode::Stop2: {
    const uint32_t deadlineMs = computeTimeToNextDeadlineMs();
    if (deadlineMs <= minStop2DurationMs_) {
      // Fast shallow sleep if deadline is within minimum overhead threshold
      enterSleep();
      return true;
    }

    enterStop2(deadlineMs);
    return true;
  }
  }

  return true;
}

} // namespace power
