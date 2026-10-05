#pragma once

#include "peripherals/UsbDeviceTypes.h"

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

// The real header is empty unless the CMSIS device header defines USB_BASE.
#define USB_BASE 0x40005C00u

namespace hw {

/**
 * Test double for hw::UsbDevice (system-stm32, families/stm32l0). It records
 * the calls of the controller and lets a test raise hardware events through
 * the registered callback, as the USB interrupt does on the target.
 */
class UsbDevice {
public:
  static constexpr uint8_t EndpointCount = 8;
  static constexpr uint16_t Ep0MaxPacketSize = 64;

  struct OpenCall {
    uint8_t address;
    usb::EndpointType type;
    uint16_t maxPacketSize;
  };

  usb::EventCallback callback{nullptr};
  void *context{nullptr};
  bool begun{false};
  bool connected{false};
  // -1: setAddress() not called.
  int address{-1};
  bool openResult{true};
  std::vector<OpenCall> openCalls;
  int closeAllCount{0};
  std::map<uint8_t, std::vector<uint8_t>> lastIn;
  bool startInResult{true};
  std::vector<uint8_t> armedOut;
  std::map<uint8_t, std::vector<uint8_t>> outData;
  std::map<uint8_t, bool> stalled;
  std::vector<uint8_t> nakCalls;
  int setStallCalls{0};
  // Models the toggle-write race of the real driver: the next setStall(true)
  // leaves the endpoint not stalled.
  bool loseNextStall{false};

  void begin(usb::EventCallback cb, void *ctx) {
    callback = cb;
    context = ctx;
    begun = true;
  }
  void end() {
    begun = false;
    connected = false;
    callback = nullptr;
  }

  void connect() { connected = true; }
  void disconnect() { connected = false; }
  bool isConnected() const { return connected; }
  void setAddress(uint8_t value) { address = value; }

  [[nodiscard]] bool openEndpoint(uint8_t ep, usb::EndpointType type,
                                  uint16_t maxPacketSize) {
    openCalls.push_back({ep, type, maxPacketSize});
    return openResult;
  }

  void closeAllEndpoints() { ++closeAllCount; }

  [[nodiscard]] bool startIn(uint8_t ep, std::span<const uint8_t> data) {
    if (!startInResult) {
      return false;
    }
    lastIn[ep].assign(data.begin(), data.end());
    return true;
  }

  bool armOut(uint8_t ep) {
    armedOut.push_back(ep);
    return true;
  }

  uint16_t readOut(uint8_t ep, std::span<uint8_t> buffer) const {
    auto it = outData.find(ep);
    if (it == outData.end()) {
      return 0;
    }
    size_t size =
        it->second.size() < buffer.size() ? it->second.size() : buffer.size();
    for (size_t i = 0; i < size; ++i) {
      buffer[i] = it->second[i];
    }
    return static_cast<uint16_t>(size);
  }

  void setStall(uint8_t ep, bool stall) {
    ++setStallCalls;
    if (stall && loseNextStall) {
      loseNextStall = false;
      stalled[ep] = false;
      return;
    }
    stalled[ep] = stall;
  }
  bool isStalled(uint8_t ep) const {
    auto it = stalled.find(ep);
    return it != stalled.end() && it->second;
  }

  void setNak(uint8_t ep) { nakCalls.push_back(ep); }

  // Test helper: raises an event as the interrupt handler does.
  void raise(const usb::Event &event) {
    if (callback != nullptr) {
      callback(context, event);
    }
  }
};

} // namespace hw
