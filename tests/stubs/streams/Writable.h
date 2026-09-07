#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

/**
 * Minimal stand-in for libArT's Writable: only the parts SerialWritable
 * derives from and returns. The error constants must match
 * libart/src/streams/Writable.h.
 */
class Writable {
public:
  using Error = int32_t;

  enum { CouldNotWrite = 0, Overflow };

  Writable() = default;
  virtual ~Writable() = default;

  virtual std::expected<size_t, Error> write(std::span<const uint8_t> data) = 0;
  virtual std::expected<void, Error> flush() = 0;
};
