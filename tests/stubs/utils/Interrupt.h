#pragma once

#include <mutex>

namespace Interrupt {

inline std::recursive_mutex g_critical_section_mutex;

class CriticalSection {
public:
  CriticalSection() { g_critical_section_mutex.lock(); }
  ~CriticalSection() { g_critical_section_mutex.unlock(); }
  CriticalSection(const CriticalSection &) = delete;
  CriticalSection &operator=(const CriticalSection &) = delete;
};

} // namespace Interrupt
