#pragma once

// Helpers of the hardware test image: pins, the DWT cycle counter and
// tolerance checks.

#include <cmsis_device.h>
#include <cstdint>

#include <peripherals/GpioPin.h>

namespace hwtest {

using Pin = hw::GpioPin;

void alternate(GPIO_TypeDef *port, uint8_t pin, Pin::AlternateFunction af, Pin::Pull pull = Pin::Pull::None);

/// Starts the DWT cycle counter (core clock cycles).
void startCycleCounter();

inline uint32_t cycles() {
   return DWT->CYCCNT;
}

/// Core clock cycles of `us` microseconds.
inline uint32_t microsecondsToCycles(uint32_t us) {
   return SystemCoreClock / 1000000U * us;
}

/// true if `value` differs from `expected` by `ppm` parts per million or less.
bool within(uint64_t value, uint64_t expected, uint32_t ppm);

/// Polls `done` until it returns true. false after `timeoutUs` microseconds.
template <typename Done>
bool waitFor(Done done, uint32_t timeoutUs) {
   const uint32_t start = cycles();
   const uint32_t limit = microsecondsToCycles(timeoutUs);
   while(!done()) {
      if(cycles() - start > limit) {
         return done();
      }
   }
   return true;
}

/// A peripheral register as a DMA address.
inline void *address(const volatile uint32_t &reg) {
   return const_cast<uint32_t *>(&reg);
}

} // namespace hwtest
