#pragma once

#include <cstdint>
#include <vector>

namespace hw {

/**
 * Test double for the TX side of hw::Usart: the bytes handed to the
 * hardware, the TXE/TC flags and their interrupt enables. The real methods
 * are const (they only write registers through a pointer), so the state here
 * is mutable.
 */
class Usart {
public:
  mutable std::vector<uint8_t> transmitted;
  mutable bool txRegisterEmpty{true};
  mutable bool transferComplete{true};
  mutable bool txEmptyInterruptEnabled{false};
  mutable bool txCompleteInterruptEnabled{false};

  void setData(uint8_t data) const {
    transmitted.push_back(data);
    transferComplete = false;
  }

  [[nodiscard]] bool isTxRegisterEmpty() const { return txRegisterEmpty; }
  [[nodiscard]] bool isTransferComplete() const { return transferComplete; }

  void enableTxEmptyInterrupt() const { txEmptyInterruptEnabled = true; }
  void disableTxEmptyInterrupt() const { txEmptyInterruptEnabled = false; }
  [[nodiscard]] bool isTxEmptyInterruptEnabled() const {
    return txEmptyInterruptEnabled;
  }

  void enableTxCompleteInterrupt() const { txCompleteInterruptEnabled = true; }
  void disableTxCompleteInterrupt() const {
    txCompleteInterruptEnabled = false;
  }
  [[nodiscard]] bool isTxCompleteInterruptEnabled() const {
    return txCompleteInterruptEnabled;
  }
  void clearTxCompleteFlag() const { transferComplete = false; }
};

} // namespace hw
