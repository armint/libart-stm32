#pragma once

#include <array>
#include <cstdint>

/**
 * Stand-in for system-stm32's peripherals/UsbDeviceTypes.h with the same
 * types. The real header has no hardware dependency, but the host tests do
 * not check out system-stm32. The cross build uses the real header.
 */
namespace hw::usb {

constexpr uint8_t EndpointDirectionIn = 0x80;
constexpr uint8_t EndpointNumberMask = 0x0F;

constexpr uint8_t endpointNumber(uint8_t address) {
  return address & EndpointNumberMask;
}

constexpr bool isInEndpoint(uint8_t address) {
  return (address & EndpointDirectionIn) != 0;
}

enum class EndpointType : uint8_t {
  Control,
  Isochronous,
  Bulk,
  Interrupt,
};

enum class EventType : uint8_t {
  Reset,
  Setup,
  Out,
  InComplete,
  Suspend,
  Resume,
};

struct Event {
  EventType type;
  uint8_t endpoint;
  uint16_t size;
  std::array<uint8_t, 8> setup;
};

using EventCallback = void (*)(void *context, const Event &event);

} // namespace hw::usb
