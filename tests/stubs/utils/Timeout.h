#pragma once

#include <cstdint>

namespace utils {

/**
 * Stand-in for libArT's utils::Timeout, driven by a test clock the tests
 * advance by hand instead of a platform timestamp source.
 */
inline int64_t g_test_now_ms = 0;

inline void advanceTestClock(uint32_t ms) {
  g_test_now_ms += static_cast<int64_t>(ms);
}

class Timeout {
public:
  explicit Timeout(uint32_t time) : deadline_(g_test_now_ms + time) {}

  [[nodiscard]] bool hasExpired() const { return deadline_ <= g_test_now_ms; }

  [[nodiscard]] uint32_t getDurationLeft() const {
    if (deadline_ <= g_test_now_ms) {
      return 0;
    }
    return static_cast<uint32_t>(deadline_ - g_test_now_ms);
  }

private:
  int64_t deadline_;
};

} // namespace utils
