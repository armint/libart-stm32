#include "usb/Stm32UsbDeviceController.h"

#include <usb/Device.h>
#include <utils/RingBuffer.h>

#include <array>
#include <cstdint>
#include <doctest/doctest.h>
#include <string>
#include <vector>

namespace {

using hw::usb::Event;
using hw::usb::EventType;

/// Records the events that the controller delivers, in order.
class RecordingEvents : public art::usb::ControllerEvents {
public:
  std::vector<std::string> log;
  art::usb::SetupPacket lastSetup{};
  std::vector<uint8_t> lastOut;

  void busReset() override { log.emplace_back("reset"); }
  void suspended() override { log.emplace_back("suspend"); }
  void resumed() override { log.emplace_back("resume"); }
  void setupReceived(const art::usb::SetupPacket &setup) override {
    lastSetup = setup;
    log.emplace_back("setup");
  }
  void outPacketReceived(uint8_t ep, art::span<const uint8_t> packet) override {
    lastOut.assign(packet.begin(), packet.end());
    log.push_back("out " + std::to_string(ep));
  }
  void inPacketSent(uint8_t ep) override {
    log.push_back("in " + std::to_string(ep));
  }
};

Event reset() { return Event{EventType::Reset, 0, 0, {}}; }
Event setup(std::array<uint8_t, 8> bytes) {
  return Event{EventType::Setup, 0, 8, bytes};
}
Event out(uint8_t ep, uint16_t size) {
  return Event{EventType::Out, ep, size, {}};
}
Event inComplete(uint8_t ep) { return Event{EventType::InComplete, ep, 0, {}}; }

constexpr std::array<uint8_t, 8> kSetAddress5{0x00, 0x05, 0x05, 0x00,
                                              0x00, 0x00, 0x00, 0x00};
constexpr std::array<uint8_t, 8> kGetDeviceDescriptor{0x80, 0x06, 0x00, 0x01,
                                                      0x00, 0x00, 0x40, 0x00};

struct Fixture {
  hw::UsbDevice hardware;
  RingBuffer<Event, 16> queue;
  Stm32UsbDeviceController controller{hardware, queue};
  RecordingEvents events;

  Fixture() {
    controller.setEventHandler(&events);
    controller.begin();
  }

  void openEp0() {
    REQUIRE(
        controller.openEndpoint({0x00, art::usb::EndpointType::Control, 64}));
  }
};

} // namespace

TEST_CASE("Stm32UsbDeviceController starts and connects the peripheral") {
  Fixture f;
  CHECK(f.hardware.begun);
  CHECK(f.hardware.callback != nullptr);
  f.controller.connect();
  CHECK(f.hardware.connected);
  f.controller.disconnect();
  CHECK_FALSE(f.hardware.connected);
  f.controller.end();
  CHECK_FALSE(f.hardware.begun);
}

TEST_CASE("Stm32UsbDeviceController delivers events in poll(), not in the "
          "interrupt") {
  Fixture f;
  f.hardware.raise(reset());
  f.hardware.raise(setup(kGetDeviceDescriptor));
  CHECK(f.events.log.empty());

  f.controller.poll();
  CHECK(f.events.log == std::vector<std::string>{"reset", "setup"});
  CHECK(f.events.lastSetup.bmRequestType == 0x80);
  CHECK(f.events.lastSetup.bRequest == 0x06);
  CHECK(f.events.lastSetup.wValue == 0x0100);
  CHECK(f.events.lastSetup.wLength == 64);
}

TEST_CASE("Stm32UsbDeviceController reports suspend and resume") {
  Fixture f;
  f.hardware.raise(Event{EventType::Suspend, 0, 0, {}});
  f.hardware.raise(Event{EventType::Resume, 0, 0, {}});
  f.controller.poll();
  CHECK(f.events.log == std::vector<std::string>{"suspend", "resume"});
}

TEST_CASE("Stm32UsbDeviceController reads an OUT packet when it delivers it") {
  Fixture f;
  REQUIRE(f.controller.openEndpoint({0x02, art::usb::EndpointType::Bulk, 64}));
  f.hardware.raise(out(0x02, 3));
  // The packet stays in packet memory until poll(): the endpoint NAKs.
  f.hardware.outData[0x02] = {'a', 'b', 'c'};
  f.controller.poll();
  CHECK(f.events.log == std::vector<std::string>{"out 2"});
  CHECK(f.events.lastOut == std::vector<uint8_t>{'a', 'b', 'c'});
}

TEST_CASE("Stm32UsbDeviceController drops packet events of closed endpoints") {
  Fixture f;
  REQUIRE(f.controller.openEndpoint({0x02, art::usb::EndpointType::Bulk, 64}));
  REQUIRE(f.controller.openEndpoint({0x82, art::usb::EndpointType::Bulk, 64}));
  f.hardware.raise(out(0x02, 0));
  f.hardware.raise(inComplete(0x82));
  f.controller.closeEndpoint(0x02);
  f.controller.closeEndpoint(0x82);
  f.controller.poll();
  CHECK(f.events.log.empty());
}

TEST_CASE("Stm32UsbDeviceController drops the events queued before a reset") {
  Fixture f;
  f.openEp0();
  REQUIRE(f.controller.openEndpoint({0x02, art::usb::EndpointType::Bulk, 64}));
  f.hardware.raise(setup(kGetDeviceDescriptor));
  f.hardware.raise(out(0x02, 0));
  f.hardware.raise(Event{EventType::Suspend, 0, 0, {}});
  f.hardware.raise(reset());
  f.hardware.raise(setup(kGetDeviceDescriptor));
  f.controller.poll();
  CHECK(f.events.log == std::vector<std::string>{"reset", "setup"});
}

TEST_CASE("Stm32UsbDeviceController closes all endpoints on a reset") {
  Fixture f;
  f.openEp0();
  REQUIRE(f.controller.openEndpoint({0x81, art::usb::EndpointType::Bulk, 64}));
  f.hardware.raise(reset());
  f.controller.poll();
  CHECK_FALSE(f.controller.startInTransfer(0x81, {}));
  CHECK_FALSE(f.controller.startInTransfer(0x80, {}));
  CHECK(f.hardware.lastIn.empty());
}

TEST_CASE("Stm32UsbDeviceController applies SET_ADDRESS in the interrupt "
          "after the status stage") {
  Fixture f;
  f.openEp0();
  f.hardware.raise(setup(kSetAddress5));
  f.controller.poll();

  f.controller.setAddress(5);
  CHECK(f.hardware.address == -1);

  f.hardware.raise(inComplete(0x80));
  CHECK(f.hardware.address == 5);
  f.controller.poll();
  CHECK(f.events.log == std::vector<std::string>{"setup", "in 128"});

  // Applied once only.
  f.hardware.address = -1;
  f.hardware.raise(inComplete(0x80));
  CHECK(f.hardware.address == -1);
}

TEST_CASE("Stm32UsbDeviceController drops a pending address on SETUP and "
          "reset") {
  Fixture f;
  f.openEp0();

  SUBCASE("SETUP after setAddress()") {
    f.controller.setAddress(5);
    f.hardware.raise(setup(kGetDeviceDescriptor));
  }
  SUBCASE("reset after setAddress()") {
    f.controller.setAddress(5);
    f.hardware.raise(reset());
  }
  SUBCASE("SETUP queued before setAddress()") {
    f.hardware.raise(setup(kGetDeviceDescriptor));
    f.controller.setAddress(5);
  }
  SUBCASE("reset queued before setAddress()") {
    f.hardware.raise(reset());
    f.controller.setAddress(5);
  }

  f.hardware.raise(inComplete(0x80));
  CHECK(f.hardware.address == -1);
}

TEST_CASE("Stm32UsbDeviceController opens EP0 without touching the hardware") {
  Fixture f;
  CHECK_FALSE(
      f.controller.openEndpoint({0x00, art::usb::EndpointType::Control, 8}));
  CHECK_FALSE(
      f.controller.openEndpoint({0x00, art::usb::EndpointType::Bulk, 64}));
  CHECK_FALSE(
      f.controller.openEndpoint({0x80, art::usb::EndpointType::Control, 64}));
  CHECK_FALSE(f.controller.startInTransfer(0x80, {}));

  f.openEp0();
  CHECK(f.hardware.openCalls.empty());
  CHECK(f.controller.startInTransfer(0x80, {}));
  f.controller.armOutTransfer(0x00);
  CHECK(f.hardware.armedOut == std::vector<uint8_t>{0x00});
}

TEST_CASE("Stm32UsbDeviceController opens other endpoints in the hardware") {
  Fixture f;
  REQUIRE(
      f.controller.openEndpoint({0x81, art::usb::EndpointType::Interrupt, 16}));
  REQUIRE(f.hardware.openCalls.size() == 1);
  CHECK(f.hardware.openCalls[0].address == 0x81);
  CHECK(f.hardware.openCalls[0].type == hw::usb::EndpointType::Interrupt);
  CHECK(f.hardware.openCalls[0].maxPacketSize == 16);

  const std::array<uint8_t, 2> data{1, 2};
  CHECK(f.controller.startInTransfer(0x81, data));
  CHECK(f.hardware.lastIn[0x81] == std::vector<uint8_t>{1, 2});
  // The OUT direction of endpoint 1 is not open.
  f.controller.armOutTransfer(0x01);
  CHECK(f.hardware.armedOut.empty());

  f.hardware.openResult = false;
  CHECK_FALSE(
      f.controller.openEndpoint({0x02, art::usb::EndpointType::Bulk, 64}));
  CHECK_FALSE(f.controller.startInTransfer(0x82, data));
}

TEST_CASE("Stm32UsbDeviceController frees the packet memory when the last "
          "endpoint closes") {
  Fixture f;
  f.openEp0();
  REQUIRE(f.controller.openEndpoint({0x02, art::usb::EndpointType::Bulk, 64}));
  REQUIRE(f.controller.openEndpoint({0x82, art::usb::EndpointType::Bulk, 64}));

  f.controller.closeEndpoint(0x02);
  CHECK(f.hardware.closeAllCount == 0);
  CHECK(f.hardware.nakCalls == std::vector<uint8_t>{0x02, 0x02});

  f.controller.closeEndpoint(0x82);
  CHECK(f.hardware.closeAllCount == 1);
  CHECK_FALSE(f.controller.startInTransfer(0x82, {}));

  // Closing a closed endpoint has no effect; EP0 stays open.
  f.controller.closeEndpoint(0x82);
  CHECK(f.hardware.closeAllCount == 1);
  CHECK(f.controller.startInTransfer(0x80, {}));
}

TEST_CASE("Stm32UsbDeviceController stalls and clears endpoints") {
  Fixture f;
  f.openEp0();
  REQUIRE(f.controller.openEndpoint({0x82, art::usb::EndpointType::Bulk, 64}));

  REQUIRE(f.controller.openEndpoint({0x02, art::usb::EndpointType::Bulk, 64}));
  const std::array<uint8_t, 1> data{7};

  SUBCASE("stall") {
    f.controller.setEndpointStall(0x82, true);
    CHECK(f.hardware.isStalled(0x82));
    CHECK(f.hardware.setStallCalls == 1);
  }
  SUBCASE("stall on a VALID endpoint while a transaction completes") {
    REQUIRE(f.controller.startInTransfer(0x82, data));
    f.hardware.raceOnNextWrite = true;
    f.controller.setEndpointStall(0x82, true);
    CHECK(f.hardware.isStalled(0x82));
    CHECK_FALSE(f.hardware.valid[0x82]);
  }
  SUBCASE("clear on a stalled endpoint") {
    f.controller.setEndpointStall(0x82, true);
    f.controller.setEndpointStall(0x82, false);
    CHECK_FALSE(f.hardware.isStalled(0x82));
    CHECK(f.hardware.toggle[0x82] == 0);
  }
  SUBCASE("clear on a VALID endpoint resets the toggle") {
    // CLEAR_FEATURE(ENDPOINT_HALT) on an endpoint that is not halted.
    f.hardware.toggle[0x02] = 1;
    f.controller.armOutTransfer(0x02);
    f.hardware.raceOnNextWrite = true;
    f.controller.setEndpointStall(0x02, false);
    CHECK(f.hardware.toggle[0x02] == 0);
    CHECK_FALSE(f.hardware.valid[0x02]);
    CHECK_FALSE(f.hardware.isStalled(0x02));
  }
  SUBCASE("EP0 is written once") {
    f.controller.setEndpointStall(0x80, true);
    f.controller.setEndpointStall(0x00, false);
    CHECK(f.hardware.setStallCalls == 2);
    CHECK(f.hardware.nakCalls.empty());
  }
  SUBCASE("closed endpoint") {
    f.controller.setEndpointStall(0x01, true);
    CHECK(f.hardware.setStallCalls == 0);
  }
}

TEST_CASE("Stm32UsbDeviceController counts the events that do not fit") {
  hw::UsbDevice hardware;
  RingBuffer<Event, 4> queue;
  Stm32UsbDeviceController controller{hardware, queue};
  RecordingEvents events;
  controller.setEventHandler(&events);
  controller.begin();

  for (int i = 0; i < 5; ++i) {
    hardware.raise(Event{EventType::Suspend, 0, 0, {}});
  }
  CHECK(controller.droppedEvents() == 2);
  controller.poll();
  CHECK(events.log.size() == 3);
}

TEST_CASE("Stm32UsbDeviceController begin() clears stale events") {
  Fixture f;
  f.hardware.raise(reset());
  f.controller.begin();
  f.controller.poll();
  CHECK(f.events.log.empty());
}

TEST_CASE("Stm32UsbDeviceController drains the queue without a handler") {
  Fixture f;
  f.controller.setEventHandler(nullptr);
  f.hardware.raise(Event{EventType::Suspend, 0, 0, {}});
  f.controller.poll();
  f.controller.setEventHandler(&f.events);
  f.controller.poll();
  CHECK(f.events.log.empty());
}

TEST_CASE("Stm32UsbDeviceController runs usb::Device through enumeration") {
  // A device with one interface and two bulk endpoints.
  static constexpr std::array<uint8_t, 18> kDevice{
      18, 1, 0x00, 0x02, 0, 0, 0, 64, 0x03, 0x04, 0x3B, 0x9B, 0, 1, 0, 0, 0, 1};
  static constexpr std::array<uint8_t, 32> kConfiguration{
      9, 2, 32,   0,    1,  1,    0, 0x80, 50, // configuration
      9, 4, 0,    0,    2,  0xFF, 0, 0,    0,  // interface 0
      7, 5, 0x81, 0x02, 64, 0,    0,           // EP1 IN bulk
      7, 5, 0x01, 0x02, 64, 0,    0};          // EP1 OUT bulk
  const art::usb::Descriptors descriptors{kDevice, kConfiguration, {}};

  Fixture f;
  art::usb::Device device(f.controller, descriptors);
  device.start();
  CHECK(f.hardware.connected);

  f.hardware.raise(reset());
  f.controller.poll();
  CHECK(device.state() == art::usb::DeviceState::Default);

  // GET_DESCRIPTOR(device): data stage, then the status OUT.
  f.hardware.raise(setup(kGetDeviceDescriptor));
  f.controller.poll();
  CHECK(f.hardware.lastIn[0x80] ==
        std::vector<uint8_t>(kDevice.begin(), kDevice.end()));
  f.hardware.raise(inComplete(0x80));
  f.controller.poll();
  f.hardware.raise(out(0x00, 0));
  f.controller.poll();

  // SET_ADDRESS(5): the hardware gets the address after the status stage,
  // before the main loop runs.
  f.hardware.raise(setup(kSetAddress5));
  f.controller.poll();
  CHECK(f.hardware.lastIn[0x80].empty());
  CHECK(f.hardware.address == -1);
  f.hardware.raise(inComplete(0x80));
  CHECK(f.hardware.address == 5);
  f.controller.poll();
  CHECK(device.state() == art::usb::DeviceState::Address);
  CHECK(device.address() == 5);

  // SET_CONFIGURATION(1) opens the bulk endpoints, (0) closes them again.
  f.hardware.raise(setup({0x00, 0x09, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}));
  f.controller.poll();
  CHECK(device.state() == art::usb::DeviceState::Configured);
  REQUIRE(f.hardware.openCalls.size() == 2);
  CHECK(f.hardware.openCalls[0].address == 0x81);
  CHECK(f.hardware.openCalls[1].address == 0x01);
  f.hardware.raise(inComplete(0x80));
  f.controller.poll();

  const int closeAllBefore = f.hardware.closeAllCount;
  f.hardware.raise(setup({0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}));
  f.controller.poll();
  CHECK(device.state() == art::usb::DeviceState::Address);
  CHECK(f.hardware.closeAllCount == closeAllBefore + 1);
}

TEST_CASE("Stm32UsbDeviceController ignores EP0 calls of a transfer that a "
          "newer SETUP aborted") {
  /// Handles SETUP A while SETUP B arrives, then answers A on EP0.
  class AbortedTransfer : public RecordingEvents {
  public:
    hw::UsbDevice *hardware{nullptr};
    Stm32UsbDeviceController *controller{nullptr};
    bool newerEvent{true};
    bool reset{false};
    int setups{0};
    bool started{false};

    void setupReceived(const art::usb::SetupPacket &packet) override {
      RecordingEvents::setupReceived(packet);
      if (++setups > 1) {
        return;
      }
      if (newerEvent) {
        hardware->raise(reset ? ::reset() : setup(kGetDeviceDescriptor));
      }
      const std::array<uint8_t, 2> data{1, 2};
      started = controller->startInTransfer(0x80, data);
      controller->armOutTransfer(0x00);
      controller->setEndpointStall(0x80, true);
      controller->setEndpointStall(0x00, true);
    }
  };

  Fixture f;
  f.openEp0();
  AbortedTransfer handler;
  handler.hardware = &f.hardware;
  handler.controller = &f.controller;
  f.controller.setEventHandler(&handler);

  SUBCASE("newer SETUP") {}
  SUBCASE("reset") { handler.reset = true; }
  SUBCASE("no newer event: control") { handler.newerEvent = false; }

  f.hardware.raise(setup(kGetDeviceDescriptor));
  f.controller.poll();
  if (handler.newerEvent) {
    CHECK_FALSE(handler.started);
    CHECK(f.hardware.lastIn.empty());
    CHECK(f.hardware.armedOut.empty());
    CHECK(f.hardware.setStallCalls == 0);
  } else {
    CHECK(handler.started);
    CHECK(f.hardware.lastIn.count(0x80) == 1);
    CHECK(f.hardware.armedOut == std::vector<uint8_t>{0x00});
    CHECK(f.hardware.setStallCalls == 2);
  }

  // The handler of SETUP B itself is not blocked.
  if (!handler.reset && handler.newerEvent) {
    CHECK(handler.setups == 2);
    CHECK(f.controller.startInTransfer(0x80, {}));
  }
}

TEST_CASE("Stm32UsbDeviceController frees endpoints that a handler opened "
          "during a reset") {
  /// SET_CONFIGURATION handler that the reset interrupt interrupts.
  class Configure : public RecordingEvents {
  public:
    hw::UsbDevice *hardware{nullptr};
    Stm32UsbDeviceController *controller{nullptr};

    void setupReceived(const art::usb::SetupPacket &packet) override {
      RecordingEvents::setupReceived(packet);
      hardware->raise(reset());
      opened =
          controller->openEndpoint({0x81, art::usb::EndpointType::Bulk, 64});
    }
    bool opened{false};
  };

  Fixture f;
  f.openEp0();
  Configure handler;
  handler.hardware = &f.hardware;
  handler.controller = &f.controller;
  f.controller.setEventHandler(&handler);

  f.hardware.raise(setup({0x00, 0x09, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}));
  f.controller.poll();
  CHECK(handler.opened);
  CHECK(handler.log == std::vector<std::string>{"setup", "reset"});
  CHECK(f.hardware.closeAllCount == 1);
  CHECK_FALSE(f.controller.startInTransfer(0x81, {}));
}

TEST_CASE("Stm32UsbDeviceController drops packet events of an endpoint that "
          "was closed and opened again") {
  Fixture f;
  f.openEp0();
  REQUIRE(f.controller.openEndpoint({0x01, art::usb::EndpointType::Bulk, 64}));
  REQUIRE(f.controller.openEndpoint({0x02, art::usb::EndpointType::Bulk, 64}));
  REQUIRE(f.controller.openEndpoint({0x82, art::usb::EndpointType::Bulk, 64}));
  f.hardware.raise(out(0x02, 0));
  f.hardware.raise(inComplete(0x82));
  f.hardware.raise(out(0x01, 0));

  // SET_INTERFACE on the interface of EP2.
  f.controller.closeEndpoint(0x02);
  f.controller.closeEndpoint(0x82);
  REQUIRE(f.controller.openEndpoint({0x02, art::usb::EndpointType::Bulk, 64}));
  REQUIRE(f.controller.openEndpoint({0x82, art::usb::EndpointType::Bulk, 64}));
  f.hardware.raise(inComplete(0x82));

  f.controller.poll();
  CHECK(f.events.log == std::vector<std::string>{"out 1", "in 130"});
}

TEST_CASE("Stm32UsbDeviceController arms an OUT endpoint only after its "
          "queued packet is delivered") {
  /// Arms the endpoint again from the packet handler, as a function does.
  class Rearm : public RecordingEvents {
  public:
    Stm32UsbDeviceController *controller{nullptr};
    bool rearm{false};
    void outPacketReceived(uint8_t ep,
                           art::span<const uint8_t> packet) override {
      RecordingEvents::outPacketReceived(ep, packet);
      if (rearm) {
        controller->armOutTransfer(ep);
      }
    }
  };

  Fixture f;
  Rearm handler;
  handler.controller = &f.controller;
  f.controller.setEventHandler(&handler);
  REQUIRE(f.controller.openEndpoint({0x02, art::usb::EndpointType::Bulk, 64}));

  f.hardware.raise(out(0x02, 3));
  f.hardware.outData[0x02] = {'a', 'b', 'c'};
  // For example after CLEAR_FEATURE(ENDPOINT_HALT), before poll().
  f.controller.armOutTransfer(0x02);
  CHECK(f.hardware.armedOut.empty());

  SUBCASE("the controller arms after delivery") {}
  SUBCASE("the handler arms during delivery") { handler.rearm = true; }

  f.controller.poll();
  CHECK(handler.lastOut == std::vector<uint8_t>{'a', 'b', 'c'});
  CHECK(f.hardware.armedOut == std::vector<uint8_t>{0x02});

  // Nothing queued: armed at once.
  f.controller.armOutTransfer(0x02);
  CHECK(f.hardware.armedOut.size() == 2);
}

TEST_CASE("Stm32UsbDeviceController opens one OUT endpoint double-buffered") {
  Fixture f;
  f.controller.setDoubleBufferedOut(0x03);
  REQUIRE(f.controller.openEndpoint({0x03, art::usb::EndpointType::Bulk, 64}));
  REQUIRE(f.controller.openEndpoint({0x01, art::usb::EndpointType::Bulk, 64}));
  REQUIRE(f.controller.openEndpoint({0x83, art::usb::EndpointType::Bulk, 64}));
  REQUIRE(f.hardware.openCalls.size() == 3);
  CHECK(f.hardware.openCalls[0].doubleBuffered);
  CHECK_FALSE(f.hardware.openCalls[1].doubleBuffered);
  CHECK_FALSE(f.hardware.openCalls[2].doubleBuffered);

  // Only a bulk endpoint, and none without setDoubleBufferedOut().
  Fixture g;
  g.controller.setDoubleBufferedOut(0x02);
  REQUIRE(
      g.controller.openEndpoint({0x02, art::usb::EndpointType::Interrupt, 8}));
  CHECK_FALSE(g.hardware.openCalls[0].doubleBuffered);
  Fixture h;
  REQUIRE(h.controller.openEndpoint({0x02, art::usb::EndpointType::Bulk, 64}));
  CHECK_FALSE(h.hardware.openCalls[0].doubleBuffered);
}

TEST_CASE("Stm32UsbDeviceController delivers a packet that armOut() reports") {
  /// Arms the endpoint again from the packet handler, as a function does.
  class Rearm : public RecordingEvents {
  public:
    Stm32UsbDeviceController *controller{nullptr};
    std::vector<std::vector<uint8_t>> packets;
    void outPacketReceived(uint8_t ep,
                           art::span<const uint8_t> packet) override {
      RecordingEvents::outPacketReceived(ep, packet);
      packets.emplace_back(packet.begin(), packet.end());
      controller->armOutTransfer(ep);
    }
  };

  Fixture f;
  Rearm handler;
  handler.controller = &f.controller;
  f.controller.setEventHandler(&handler);
  f.controller.setDoubleBufferedOut(0x03);
  REQUIRE(f.controller.openEndpoint({0x03, art::usb::EndpointType::Bulk, 64}));
  REQUIRE(f.controller.openEndpoint({0x82, art::usb::EndpointType::Bulk, 64}));

  // Packet 1 is reported in the interrupt. Packet 2 came in the second
  // buffer before the arm: armOut() of packet 1 reports it in the main loop.
  f.hardware.raise(out(0x03, 1));
  f.hardware.outData[0x03] = {1};
  f.hardware.waitingOut[0x03] = {2, 2};
  f.hardware.raise(inComplete(0x82));
  f.controller.poll();
  // In order: the IN event came before armOut() reported packet 2.
  CHECK(handler.log == std::vector<std::string>{"out 3", "in 130", "out 3"});
  CHECK(handler.packets == std::vector<std::vector<uint8_t>>{{1}, {2, 2}});
  CHECK(f.hardware.armedOut == std::vector<uint8_t>{0x03, 0x03});
  CHECK(f.controller.droppedEvents() == 0);
}

TEST_CASE("Stm32UsbDeviceController forgets a deferred arm when the endpoint "
          "closes") {
  Fixture f;
  REQUIRE(f.controller.openEndpoint({0x01, art::usb::EndpointType::Bulk, 64}));
  REQUIRE(f.controller.openEndpoint({0x02, art::usb::EndpointType::Bulk, 64}));
  f.hardware.raise(out(0x02, 0));
  f.controller.armOutTransfer(0x02);
  f.controller.closeEndpoint(0x02);
  REQUIRE(f.controller.openEndpoint({0x02, art::usb::EndpointType::Bulk, 64}));
  f.controller.poll();
  CHECK(f.events.log.empty());
  CHECK(f.hardware.armedOut.empty());
}
