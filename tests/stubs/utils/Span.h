#pragma once

#include <utils/compat/span.h>

/**
 * Stand-in for system-stm32's utils/Span.h. The host tests have no
 * system-stm32 build that selects hw::span, so hw::span is libArT's
 * art::span, the type that system-stm32 selects in a consistent build.
 */
namespace hw {
using art::dynamic_extent;
using art::span;
} // namespace hw
