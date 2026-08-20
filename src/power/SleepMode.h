#pragma once

#include <cstdint>

namespace power {

enum class SleepMode : uint8_t {
  Run = 0,   // No sleep allowed
  Sleep = 1, // Shallow sleep (WFI, peripherals active)
  LPRun = 2, // Low-Power Run/Sleep (MSI <= 2MHz, LP regulator)
  Stop2 = 3  // Deepest Stop mode
};

} // namespace power
