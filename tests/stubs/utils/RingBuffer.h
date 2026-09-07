#pragma once

#include <cstddef>
#include <deque>

/**
 * Stand-in for libArT's RingBufferBase / RingBuffer with the same interface.
 * The tests are single-threaded, so the lock-free index handling of the real
 * implementation is not reproduced here.
 */
template <typename T> class RingBufferBase {
public:
  virtual ~RingBufferBase() = default;

  [[nodiscard]] virtual bool try_push(const T &value) noexcept = 0;
  [[nodiscard]] virtual bool try_pop(T &out) noexcept = 0;
  [[nodiscard]] virtual bool peek(T &out) const noexcept = 0;
  [[nodiscard]] virtual std::size_t size() const noexcept = 0;
  [[nodiscard]] virtual bool empty() const noexcept = 0;
  [[nodiscard]] virtual bool full() const noexcept = 0;
  virtual void clear() noexcept = 0;
};

template <typename T, std::size_t Size>
class RingBuffer : public RingBufferBase<T> {
  // Same constraint as the real implementation, so a test cannot pick a size
  // the firmware could not use.
  static_assert((Size & (Size - 1)) == 0, "Size must be a power of two");
  static_assert(Size >= 2, "Size must be at least 2");

public:
  // The real RingBuffer keeps one slot free to distinguish full from empty.
  static constexpr std::size_t Capacity = Size - 1u;

  [[nodiscard]] bool try_push(const T &value) noexcept override {
    if (full()) {
      return false;
    }
    items_.push_back(value);
    return true;
  }

  [[nodiscard]] bool try_pop(T &out) noexcept override {
    if (items_.empty()) {
      return false;
    }
    out = items_.front();
    items_.pop_front();
    return true;
  }

  [[nodiscard]] bool peek(T &out) const noexcept override {
    if (items_.empty()) {
      return false;
    }
    out = items_.front();
    return true;
  }

  [[nodiscard]] std::size_t size() const noexcept override {
    return items_.size();
  }
  [[nodiscard]] bool empty() const noexcept override { return items_.empty(); }
  [[nodiscard]] bool full() const noexcept override {
    return items_.size() >= Capacity;
  }
  void clear() noexcept override { items_.clear(); }

private:
  std::deque<T> items_;
};
