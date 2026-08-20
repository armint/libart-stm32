#include "I2cMaster.h"

I2cMaster::I2cMaster(I2C_TypeDef *i2cBase)
   : i2c_(i2cBase) {}

void I2cMaster::init(uint32_t clockHz) {
   i2c_.configureMaster(clockHz);
   i2c_.enable();
}

static std::expected<void, I2cError> waitIdle(const hw::I2c &i2c) {
   for(uint32_t n = 0; n < I2C_TIMEOUT_TICKS; ++n) {
      if(!i2c.isBusBusy())
         return {};
   }
   return std::unexpected(I2cError::Timeout);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::expected<void, I2cError> I2cMaster::write(uint8_t address,
                                                std::span<const uint8_t> data) const {
   if(auto r = waitIdle(i2c_); !r)
      return r;

   // Use AUTOEND=0 so each step can be checked for errors before continuing.
   i2c_.setTransferConfig(address,
                          static_cast<uint8_t>(data.size()),
                          hw::I2c::TransferDirection::Tx,
                          false);
   i2c_.generateStart();

   for(const auto byte : data) {
      // Wait for TXIS (transmit data register empty, ready for next byte)
      for(uint32_t n = 0; n < I2C_TIMEOUT_TICKS; ++n) {
         if(i2c_.isNackReceived()) {
            i2c_.clearNackFlag();
            i2c_.generateStop();
            return std::unexpected(I2cError::Nack);
         }
         if(i2c_.isTxEmpty())
            break;
         if(n == I2C_TIMEOUT_TICKS - 1u)
            return std::unexpected(I2cError::Timeout);
      }
      i2c_.write(byte);
   }

   // Wait for TC (transfer complete, AUTOEND=0)
   for(uint32_t n = 0; n < I2C_TIMEOUT_TICKS; ++n) {
      if(i2c_.isNackReceived()) {
         i2c_.clearNackFlag();
         i2c_.generateStop();
         return std::unexpected(I2cError::Nack);
      }
      if(i2c_.isTransferComplete())
         break;
      if(n == I2C_TIMEOUT_TICKS - 1u)
         return std::unexpected(I2cError::Timeout);
   }

   i2c_.generateStop();

   // Wait for STOPF
   for(uint32_t n = 0; n < I2C_TIMEOUT_TICKS; ++n) {
      if(i2c_.isStopSent()) {
         i2c_.clearStopFlag();
         return {};
      }
      if(n == I2C_TIMEOUT_TICKS - 1u)
         return std::unexpected(I2cError::Timeout);
   }
   return {};
}

std::expected<void, I2cError> I2cMaster::read(uint8_t address,
                                               std::span<uint8_t> data) const {
   if(auto r = waitIdle(i2c_); !r)
      return r;

   i2c_.setTransferConfig(address,
                          static_cast<uint8_t>(data.size()),
                          hw::I2c::TransferDirection::Rx,
                          false);
   i2c_.generateStart();

   for(auto &byte : data) {
      for(uint32_t n = 0; n < I2C_TIMEOUT_TICKS; ++n) {
         if(i2c_.isNackReceived()) {
            i2c_.clearNackFlag();
            i2c_.generateStop();
            return std::unexpected(I2cError::Nack);
         }
         if(i2c_.isRxNotEmpty())
            break;
         if(n == I2C_TIMEOUT_TICKS - 1u)
            return std::unexpected(I2cError::Timeout);
      }
      byte = i2c_.read();
   }

   // Wait for TC then send STOP
   for(uint32_t n = 0; n < I2C_TIMEOUT_TICKS; ++n) {
      if(i2c_.isTransferComplete())
         break;
      if(n == I2C_TIMEOUT_TICKS - 1u)
         return std::unexpected(I2cError::Timeout);
   }

   i2c_.generateStop();

   for(uint32_t n = 0; n < I2C_TIMEOUT_TICKS; ++n) {
      if(i2c_.isStopSent()) {
         i2c_.clearStopFlag();
         return {};
      }
      if(n == I2C_TIMEOUT_TICKS - 1u)
         return std::unexpected(I2cError::Timeout);
   }
   return {};
}

std::expected<void, I2cError> I2cMaster::writeRead(uint8_t address,
                                                    std::span<const uint8_t> writeData,
                                                    std::span<uint8_t> readData) const {
   if(auto r = waitIdle(i2c_); !r)
      return r;

   // Phase 1: write (no AUTOEND, no STOP — hardware holds bus for restart)
   i2c_.setTransferConfig(address,
                          static_cast<uint8_t>(writeData.size()),
                          hw::I2c::TransferDirection::Tx,
                          false);
   i2c_.generateStart();

   for(const auto byte : writeData) {
      for(uint32_t n = 0; n < I2C_TIMEOUT_TICKS; ++n) {
         if(i2c_.isNackReceived()) {
            i2c_.clearNackFlag();
            i2c_.generateStop();
            return std::unexpected(I2cError::Nack);
         }
         if(i2c_.isTxEmpty())
            break;
         if(n == I2C_TIMEOUT_TICKS - 1u)
            return std::unexpected(I2cError::Timeout);
      }
      i2c_.write(byte);
   }

   for(uint32_t n = 0; n < I2C_TIMEOUT_TICKS; ++n) {
      if(i2c_.isNackReceived()) {
         i2c_.clearNackFlag();
         i2c_.generateStop();
         return std::unexpected(I2cError::Nack);
      }
      if(i2c_.isTransferComplete())
         break;
      if(n == I2C_TIMEOUT_TICKS - 1u)
         return std::unexpected(I2cError::Timeout);
   }

   // Phase 2: repeated-START then read
   i2c_.setTransferConfig(address,
                          static_cast<uint8_t>(readData.size()),
                          hw::I2c::TransferDirection::Rx,
                          false);
   i2c_.generateStart();

   for(auto &byte : readData) {
      for(uint32_t n = 0; n < I2C_TIMEOUT_TICKS; ++n) {
         if(i2c_.isNackReceived()) {
            i2c_.clearNackFlag();
            i2c_.generateStop();
            return std::unexpected(I2cError::Nack);
         }
         if(i2c_.isRxNotEmpty())
            break;
         if(n == I2C_TIMEOUT_TICKS - 1u)
            return std::unexpected(I2cError::Timeout);
      }
      byte = i2c_.read();
   }

   for(uint32_t n = 0; n < I2C_TIMEOUT_TICKS; ++n) {
      if(i2c_.isTransferComplete())
         break;
      if(n == I2C_TIMEOUT_TICKS - 1u)
         return std::unexpected(I2cError::Timeout);
   }

   i2c_.generateStop();

   for(uint32_t n = 0; n < I2C_TIMEOUT_TICKS; ++n) {
      if(i2c_.isStopSent()) {
         i2c_.clearStopFlag();
         return {};
      }
      if(n == I2C_TIMEOUT_TICKS - 1u)
         return std::unexpected(I2cError::Timeout);
   }
   return {};
}
