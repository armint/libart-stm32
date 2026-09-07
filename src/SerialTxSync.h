#pragma once

#include <cstdint>

/**
 * @brief Synchronisation hooks that let SerialWritable block while the TX
 *        ring buffer drains, instead of polling TXE itself.
 *
 * libart-stm32 is RTOS-agnostic and never calls an RTOS API directly, so the
 * application supplies an implementation of this interface -- typically a
 * mutex plus a binary semaphore. SerialWritable uses it only when
 * canBlock() reports true and falls back to its polled path otherwise, which
 * keeps logging usable from an ISR, from a critical section, and before the
 * scheduler has started.
 *
 * Two requirements on the consumer:
 *
 *  - The USART interrupt that calls handleTxInterrupt() must run at a
 *    priority the RTOS allows to call its from-ISR API. On Cortex-M with
 *    FreeRTOS that means numerically at or above
 *    configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY: a higher-priority
 *    interrupt is not masked by the kernel's critical sections, so
 *    notifyFromIsr() could corrupt kernel state.
 *  - One instance serves exactly one SerialWritable. The waiting flag lives
 *    in the SerialWritable while the lock and the notification live here, so
 *    sharing an instance between two ports lets one port's ISR wake the
 *    other port's writer.
 */
class SerialTxSync {
public:
  virtual ~SerialTxSync() = default;

  /**
   * @brief Whether the calling context is allowed to block.
   *
   * Must report false in ISR context, with interrupts masked or the
   * scheduler suspended, and before the scheduler has started.
   */
  [[nodiscard]] virtual bool canBlock() const = 0;

  /**
   * @brief Serialise task-context writers. Only called when canBlock().
   *
   * With unlock() this satisfies BasicLockable, so the driver guards itself
   * with std::lock_guard<SerialTxSync>.
   */
  virtual void lock() = 0;

  /** @brief Counterpart of lock(). */
  virtual void unlock() = 0;

  /**
   * @brief Discard a notification left over from an earlier wait.
   *
   * Called just before a wait is armed, never from inside a critical
   * section, so a plain non-blocking semaphore take is enough.
   */
  virtual void armWait() = 0;

  /**
   * @brief Block until notifyFromIsr() runs or the timeout expires.
   *
   * Spurious wake-ups are allowed: the caller re-checks the buffer level and
   * waits again if needed.
   *
   * @param timeoutMs maximum time to block, in milliseconds.
   * @return false if the wait timed out.
   */
  virtual bool wait(uint32_t timeoutMs) = 0;

  /** @brief Wake the blocked writer. Called from ISR context only. */
  virtual void notifyFromIsr() = 0;
};
