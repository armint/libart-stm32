#include "TicklessIdleHook.h"
#include <cmsis_device.h>
#include <optional>
#include <peripherals/Pwr.h>
#include <peripherals/SysTick.h>
#include <utils/Interrupt.h>

namespace power {

void TicklessIdleHook::enterSleep() {
  Interrupt::CriticalSection cs;
  __DSB();
  __WFI();
  __ISB();
}

void TicklessIdleHook::enterLPRun() {
  Interrupt::CriticalSection cs;
  // Only restore the clock if something actually lowered it. With no LPRun
  // enter callback configured this is a plain WFI that never touches the
  // clock, so invoking the restore callback would re-run a full clock
  // bring-up for nothing.
  const bool loweredClock = enterLPRunCallback_ != nullptr;
  if (loweredClock) {
    enterLPRunCallback_();
  }
  __DSB();
  __WFI();
  __ISB();
  if (loweredClock && restoreClockCallback_) {
    restoreClockCallback_();
  }
}

void TicklessIdleHook::enterStop2(uint32_t durationMs) {
  // Mask interrupts around sleep configuration and WFI to prevent preemption
  // and race conditions where an ISR readies a task between arming WUT and __WFI().
  // On ARMv7-M, __WFI() still wakes up when an enabled interrupt becomes pending.
  // Held in an optional (rather than a plain guard) so the WUTWF-timeout
  // fallback below can unmask early, before falling back to enterSleep() --
  // see the comment there.
  std::optional<Interrupt::CriticalSection> cs{std::in_place};

  // Local shorthand for the optional trace callback.
  const auto trace = [this](char c) {
    if (traceCallback_) {
      traceCallback_(c);
    }
  };

  trace('>');

  // 1. Arm RTC Wakeup Timer (clamped to avoid 32-bit overflow in (durationMs + 999u))
  constexpr uint32_t kMaxWakeupDurationMs = UINT32_MAX - 999u;
  const uint32_t clampedDurationMs =
      (durationMs > kMaxWakeupDurationMs) ? kMaxWakeupDurationMs : durationMs;
  if (!rtc_.armWakeup(clampedDurationMs)) {
    // If WUTWF poll timed out, fall back to shallow sleep rather than sleeping
    // indefinitely without a timed wakeup source. Unmask here (rather than
    // leaving cs to unwind at the end of the function) so enterSleep() runs
    // its own independent mask/unmask cycle instead of nesting inside this
    // one.
    trace('!');
    cs.reset();
    enterSleep();
    return;
  }

  trace('A');

  // 2. Read timestamp immediately prior to entering Stop 2
  const uint64_t tsBefore = rtc_.getSubSecondTimestamp();

  trace('T');

  // 3. Suspend SysTick
  hw::SysTickTimer::disable();

  trace('S');

  // 4. Configure Cortex-M deep sleep and PWR Stop 2 mode
  hw::Pwr::enableDeepSleep();
  hw::Pwr::setStopMode(hw::Pwr::StopMode::Stop2);

  trace('W');

  // 5. Enter Stop 2
  __DSB();
  __WFI();
  __ISB();

  // --- Wakeup path (now running on MSI ~4 MHz) ---

  trace('w');

  // 6. Wakeup path: Clear SLEEPDEEP
  hw::Pwr::disableDeepSleep();

  trace('C');

  // 7. Restore high-speed system clocks immediately
  if (restoreClockCallback_) {
    restoreClockCallback_();
  }

  trace('c');

  // 8. Measure elapsed time across sleep (works for RTC WUT, Radio DIO1, or any
  // EXTI)
  const uint64_t tsAfter = rtc_.getSubSecondTimestamp();
  const uint64_t elapsedMs = (tsAfter >= tsBefore) ? (tsAfter - tsBefore) : 0u;

  trace('U');

  // 9. Disarm Wakeup Timer
  rtc_.disarmWakeup();

  // 10. Resume SysTick before tick compensation and potential context switch
  hw::SysTickTimer::reset();
  hw::SysTickTimer::enable();

  trace('K');

  // 11. Compensate RTOS tick for the slept duration and trigger reschedule
  // Invoked unconditionally (even if elapsedMs == 0 across midnight wrap or sub-ms)
  // so any thread unblocked by the waking ISR is immediately rescheduled.
  if (tickCompensationCallback_) {
    tickCompensationCallback_(static_cast<uint32_t>(elapsedMs));
  }

  trace('\n');

  // 12. Restore interrupt state. Pending interrupts will fire immediately,
  // via cs unwinding at function exit.
}

bool TicklessIdleHook::handleIdle(uint32_t expectedIdleMs) {
  // Held for the whole function: every path below already unmasked
  // immediately before its own return, so cs unwinding at each return
  // reproduces that exactly. Unlike enterStop2()'s fallback, none of these
  // branches need an early unmask -- enterSleep()/enterLPRun()/enterStop2()
  // are called while still masked here, nesting correctly inside cs.
  Interrupt::CriticalSection cs;

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
    if (expectedIdleMs <= minStop2DurationMs_) {
      // Fast shallow sleep if deadline is within minimum overhead threshold
      enterSleep();
      return true;
    }

    enterStop2(expectedIdleMs);
    return true;
  }
  }

  return true;
}

} // namespace power
