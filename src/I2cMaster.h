#pragma once

#include <cmsis_device.h>
#include <expected>
#include <peripherals/I2c.h>
#include <span>

enum class I2cError : uint8_t {
   Nack,
   Timeout,
   BusError,
   ArbitrationLost,
};

/**
 * Blocking I2C master built on top of hw::I2c.
 *
 * All transfers spin-poll with a timeout counter. For measurement delays
 * (e.g. 85 ms for SHT21) use os::rtos::sysclock.sleep_for() in the caller.
 */
static constexpr uint32_t I2C_TIMEOUT_TICKS = 100'000u;

class I2cMaster {
   hw::I2c i2c_;

public:
   explicit I2cMaster(I2C_TypeDef *i2cBase);

   void init(uint32_t clockHz = 100'000u);

   /** Write bytes to a 7-bit addressed device. */
   std::expected<void, I2cError> write(uint8_t address,
                                       std::span<const uint8_t> data) const;

   /** Read bytes from a 7-bit addressed device. */
   std::expected<void, I2cError> read(uint8_t address,
                                      std::span<uint8_t> data) const;

   /**
    * Write then read without releasing the bus between the two phases
    * (repeated-START).
    */
   std::expected<void, I2cError> writeRead(uint8_t address,
                                           std::span<const uint8_t> writeData,
                                           std::span<uint8_t> readData) const;
};
