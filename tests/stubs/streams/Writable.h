#pragma once

#include <cstddef>
#include <cstdint>
#include <utils/compat/expected.h>
#include <span>

/**
 * Minimal stand-in for libArT's Writable: only the parts SerialWritable
 * derives from and returns. The error constants and the WriteSession hooks'
 * signatures must match libart/src/streams/Writable.h.
 */
class Writable {
protected:
  /** Called when a WriteSession begins. Override to take a lock or start
   *  batching for the duration of the session. Default is a no-op. */
  virtual void beginWriteSession() noexcept {}

  /** Called when a WriteSession ends. Override to release a lock or flush
   *  batching started in beginWriteSession(). Default is a no-op. */
  virtual void endWriteSession() noexcept {}

public:
  using Error = int32_t;

  enum { CouldNotWrite = 0, Overflow };

  Writable() = default;
  virtual ~Writable() = default;

  virtual art::expected<size_t, Error> write(std::span<const uint8_t> data) = 0;
  virtual art::expected<void, Error> flush() = 0;

  /**
   * RAII scope bracketing a sequence of write()/printf() calls. Nests: a
   * caller may hold one across several inner ones opened on the same task
   * (see the warning on SerialTxSync::lock()).
   */
  class WriteSession {
  public:
    explicit WriteSession(Writable &writable) noexcept : writable_(&writable) {
      writable_->beginWriteSession();
    }
    ~WriteSession() {
      if (writable_) {
        writable_->endWriteSession();
      }
    }

    WriteSession(const WriteSession &) = delete;
    WriteSession &operator=(const WriteSession &) = delete;
    WriteSession &operator=(WriteSession &&) = delete;

    WriteSession(WriteSession &&other) noexcept : writable_(other.writable_) {
      other.writable_ = nullptr;
    }

  private:
    Writable *writable_;
  };

  /** Open a WriteSession bracketing subsequent write()/printf() calls until
   *  the returned object is destroyed. See WriteSession. */
  [[nodiscard]] WriteSession writeSession() noexcept { return WriteSession(*this); }
};
