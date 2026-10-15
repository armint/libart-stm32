#pragma once

#include "peripherals/UsbDeviceTypes.h"

#include <array>
#include <cstdint>
#include <map>
#include "utils/Span.h"
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
    bool doubleBuffered;
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
  // A double-buffered endpoint: a packet that waits in the second buffer.
  // armOut() reports it through the callback, as the real driver does.
  std::map<uint8_t, std::vector<uint8_t>> waitingOut;
  std::map<uint8_t, std::vector<uint8_t>> outData;
  std::map<uint8_t, bool> stalled;
  // STAT VALID: armed by armOut() or startIn(), not completed yet.
  std::map<uint8_t, bool> valid;
  // Data toggle (DTOG) of each endpoint direction: 0 = DATA0.
  std::map<uint8_t, int> toggle;
  std::vector<uint8_t> nakCalls;
  int setStallCalls{0};
  // Models the read + toggle-write race of the real driver: the next STAT or
  // DTOG write on a VALID endpoint runs while a transaction completes. The
  // hardware sets the endpoint to NAK and flips DTOG between the read and
  // the write, so the write leaves a wrong result.
  bool raceOnNextWrite{false};
  // The race above also raises the InComplete event of an IN endpoint, as
  // the interrupt does after the write. The stub does not model an IN token
  // between two writes: only a target test can check the interrupt mask.
  bool raiseOnRace{false};

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
                                  uint16_t maxPacketSize,
                                  bool doubleBuffered = false) {
    openCalls.push_back({ep, type, maxPacketSize, doubleBuffered});
    return openResult;
  }

  void closeAllEndpoints() { ++closeAllCount; }

  [[nodiscard]] bool startIn(uint8_t ep, hw::span<const uint8_t> data) {
    if (!startInResult) {
      return false;
    }
    lastIn[ep].assign(data.begin(), data.end());
    valid[ep] = true;
    return true;
  }

  bool armOut(uint8_t ep) {
    armedOut.push_back(ep);
    valid[ep] = true;
    stalled[ep] = false;
    auto waiting = waitingOut.find(ep);
    if (waiting != waitingOut.end()) {
      outData[ep] = waiting->second;
      waitingOut.erase(waiting);
      raise(usb::Event{usb::EventType::Out, ep,
                       static_cast<uint16_t>(outData[ep].size()), {}});
    }
    return true;
  }

  uint16_t readOut(uint8_t ep, hw::span<uint8_t> buffer) const {
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
    if (racing(ep)) {
      // STALL: VALID ^ (VALID ^ STALL) after NAK gives DISABLED. Clear: the
      // DTOG write undoes the toggle of the transaction, so it ends at DATA1.
      if (!stall) {
        toggle[ep] = 1;
      }
      stalled[ep] = false;
      return;
    }
    stalled[ep] = stall;
    if (!stall) {
      toggle[ep] = 0;
    }
  }
  bool isStalled(uint8_t ep) const {
    auto it = stalled.find(ep);
    return it != stalled.end() && it->second;
  }

  void setNak(uint8_t ep) {
    nakCalls.push_back(ep);
    if (racing(ep)) {
      // NAK ^ (VALID ^ NAK) gives VALID again.
      valid[ep] = true;
      return;
    }
    valid[ep] = false;
    stalled[ep] = false;
  }

  // Test helper: the host completes a transaction on a VALID endpoint.
  void complete(uint8_t ep) {
    valid[ep] = false;
    toggle[ep] ^= 1;
  }

  // Test helper: raises an event as the interrupt handler does.
  void raise(const usb::Event &event) {
    if (callback != nullptr) {
      callback(context, event);
    }
  }

private:
  bool racing(uint8_t ep) {
    if (!raceOnNextWrite || !valid[ep]) {
      return false;
    }
    raceOnNextWrite = false;
    complete(ep);
    if (raiseOnRace && (ep & 0x80u) != 0u) {
      raise(usb::Event{usb::EventType::InComplete, ep, 0, {}});
    }
    return true;
  }
};

} // namespace hw
