#pragma once

#include <mutex>

namespace os {
namespace rtos {
namespace interrupts {

inline std::recursive_mutex g_critical_section_mutex;

class critical_section {
public:
   critical_section() {
      g_critical_section_mutex.lock();
   }
   ~critical_section() {
      g_critical_section_mutex.unlock();
   }
   critical_section(const critical_section &) = delete;
   critical_section &operator=(const critical_section &) = delete;
};

} // namespace interrupts
} // namespace rtos
} // namespace os
