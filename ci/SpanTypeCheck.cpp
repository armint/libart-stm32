// Compile-time check of the CI superproject (CMakeLists.txt in this
// directory): art::span, hw::span and art::expected must be the types of the
// configured standard. At C++23 these are std::span and std::expected. Below
// C++23 they are the own span of span-compat and libArT's own expected. A
// mismatch would give two span types in one program.

#include <cstdint>
#include <type_traits>

#include <utils/Span.h>
#include <utils/compat/expected.h>
#include <utils/compat/span.h>

static_assert(std::is_same<art::span<const uint8_t>, hw::span<const uint8_t>>::value,
              "art::span and hw::span are different types");

#if ART_USE_STD_EXPECTED_SPAN
#include <expected>
#include <span>
static_assert(__cplusplus > 202002L, "ART_USE_STD_EXPECTED_SPAN=1 needs C++23");
static_assert(std::is_same<art::span<const uint8_t>, std::span<const uint8_t>>::value,
              "art::span is not std::span at C++23");
static_assert(std::is_same<art::expected<int, int>, std::expected<int, int>>::value,
              "art::expected is not std::expected at C++23");
#else
#include <span-compat/span.h>
static_assert(__cplusplus < 202002L, "the C++17 leg does not compile at C++17");
static_assert(!SPAN_COMPAT_USE_STD_SPAN, "span-compat must use its own span");
static_assert(std::is_same<art::span<const uint8_t>, span_compat::span<const uint8_t>>::value,
              "art::span is not the span of span-compat below C++23");
#endif
