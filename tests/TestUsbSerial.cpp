#include "usb/UsbSerial.h"

#include "FakeDeviceController.h"
#include <usb/CdcAcmFunction.h>
#include <usb/DescriptorBuilder.h>
#include <usb/Device.h>

#include <array>
#include <cstdint>
#include <doctest/doctest.h>
#include <optional>
#include <string>
#include <vector>

using namespace art::usb;
using namespace art::usb::descriptor;
using art::usb::test::FakeDeviceController;
using art::usb::test::setupPacket;

namespace {

constexpr uint8_t kNotification = 0x81u;
constexpr uint8_t kDataOut = 0x02u;
constexpr uint8_t kDataIn = 0x82u;

constexpr DeviceFields makeDeviceFields() {
  DeviceFields fields{};
  fields.bDeviceClass = classcode::kMiscellaneous;
  fields.bDeviceSubClass = classcode::kMiscellaneousSubclassCommon;
  fields.bDeviceProtocol = classcode::kMiscellaneousProtocolIad;
  fields.idVendor = 0x0403u;
  fields.idProduct = 0x9B3Bu;
  return fields;
}

constexpr auto kDevice = device(makeDeviceFields());

constexpr auto kConfiguration = configuration(
    ConfigurationFields{},
    interfaceAssociation(0u, 2u, classcode::kCdc, cdc::kSubclassAcm, 0u),
    interfaceDescriptor(
        InterfaceFields{0u, 0u, classcode::kCdc, cdc::kSubclassAcm, 0u, 0u},
        cdcHeader(), cdcCallManagement(0u, 1u), cdcAcm(), cdcUnion(0u, 1u),
        endpoint(kNotification, EndpointType::Interrupt, 16u, 16u)),
    interfaceDescriptor(
        InterfaceFields{1u, 0u, classcode::kCdcData, 0u, 0u, 0u},
        endpoint(kDataOut, EndpointType::Bulk, 64u),
        endpoint(kDataIn, EndpointType::Bulk, 64u)));

/// RX queue of two packets, TX queue of two packets.
using TestCdcAcm = CdcAcmFunction<128u, 128u>;

/// A CDC-ACM device on the fake controller, and a UsbSerial on it.
class Bench {
public:
  TestCdcAcm cdc{0u, CdcAcmEndpoints{kNotification, 16u, kDataOut, kDataIn}};
  UsbSerial serial{cdc};

  Bench()
      : descriptors_{asSpan(kDevice), asSpan(kConfiguration),
                     art::span<const art::span<const uint8_t>>()},
        device_(controller_, descriptors_) {
    REQUIRE(device_.addFunction(cdc));
    device_.start();
  }

  FakeDeviceController &controller() { return controller_; }

  /// Bus reset, SET_ADDRESS 5 and SET_CONFIGURATION 1.
  void enumerate() {
    controller_.busReset();
    out(0x00u, 5u, 5u, 0u);
    out(0x00u, 9u, 1u, 0u);
    REQUIRE(cdc.isConfigured());
  }

  /// SET_CONTROL_LINE_STATE: a terminal opens (DTR) or closes the port.
  void setDtr(bool dtr) {
    out(0x21u, CdcAcmFunctionBase::kSetControlLineState,
        dtr ? CdcAcmFunctionBase::kControlLineDtr : 0u, 0u);
  }

  /// The host reads the IN packets until a short packet ends the transfer.
  std::string readTransfer() {
    std::string data;
    while (std::optional<std::vector<uint8_t>> packet =
               controller_.takeIn(kDataIn)) {
      data.append(packet->begin(), packet->end());
      if (packet->size() < CdcAcmFunctionBase::kDataPacketSize) {
        break;
      }
    }
    return data;
  }

  bool sendOut(const std::string &text) {
    return controller_.sendOut(
        kDataOut,
        art::span<const uint8_t>(reinterpret_cast<const uint8_t *>(text.data()),
                                 text.size()));
  }

private:
  void out(uint8_t requestType, uint8_t request, uint16_t value,
           uint16_t index) {
    bool ok = false;
    const FakeDeviceController::ControlResult result = controller_.controlOut(
        setupPacket(requestType, request, value, index, 0u),
        art::span<const uint8_t>(), &ok);
    REQUIRE(ok);
    REQUIRE_FALSE(result.stalled);
  }

  FakeDeviceController controller_;
  Descriptors descriptors_;
  Device device_;
};

art::span<const uint8_t> bytes(const std::string &text) {
  return {reinterpret_cast<const uint8_t *>(text.data()), text.size()};
}

} // namespace

TEST_CASE("UsbSerial drops writes before the device is configured") {
  Bench bench;
  auto result = bench.serial.write(bytes("hello"));
  REQUIRE(result.has_value());
  CHECK(*result == 5);
  CHECK(bench.serial.droppedBytes() == 5);
  CHECK_FALSE(bench.serial.isTerminalOpen());
}

TEST_CASE("UsbSerial drops writes while no terminal has the port open") {
  Bench bench;
  bench.enumerate();
  auto result = bench.serial.write(bytes("hello"));
  REQUIRE(result.has_value());
  CHECK(*result == 5);
  CHECK(bench.serial.droppedBytes() == 5);
  CHECK(bench.readTransfer().empty());
}

TEST_CASE("UsbSerial sends writes to an open terminal") {
  Bench bench;
  bench.enumerate();
  bench.setDtr(true);
  CHECK(bench.serial.isTerminalOpen());

  auto result = bench.serial.write(bytes("hello "));
  REQUIRE(result.has_value());
  CHECK(*result == 6);
  bench.serial.write(bytes("!"));
  CHECK(bench.serial.flush().has_value());

  // The first write went out at once as a short packet, the second follows.
  CHECK(bench.readTransfer() == "hello ");
  CHECK(bench.readTransfer() == "!");
  CHECK(bench.serial.droppedBytes() == 0);
}

TEST_CASE("UsbSerial drops what does not fit when the host does not read") {
  Bench bench;
  bench.enumerate();
  bench.setDtr(true);

  const std::string data(200, 'x');
  auto result = bench.serial.write(bytes(data));
  REQUIRE(result.has_value());
  // write() never fails, so that a logger does not stop.
  CHECK(*result == 200);
  CHECK(bench.serial.droppedBytes() == 200 - 128);

  bench.serial.resetDroppedBytes();
  CHECK(bench.serial.droppedBytes() == 0);

  // The host reads: the queue has room again.
  CHECK(bench.readTransfer().size() == 128);
  bench.serial.write(bytes("ok"));
  CHECK(bench.serial.droppedBytes() == 0);
}

TEST_CASE("UsbSerial drops writes after the terminal closes or the bus "
          "resets, and sends again when a terminal opens") {
  Bench bench;
  bench.enumerate();
  bench.setDtr(true);

  SUBCASE("terminal closes and opens again") {
    bench.setDtr(false);
    bench.serial.write(bytes("abc"));
    CHECK(bench.serial.droppedBytes() == 3);
    bench.setDtr(true);
  }
  SUBCASE("bus reset and new enumeration") {
    bench.controller().busReset();
    bench.serial.write(bytes("abc"));
    CHECK(bench.serial.droppedBytes() == 3);
    bench.enumerate();
    bench.setDtr(true);
  }

  bench.serial.write(bytes("new"));
  CHECK(bench.serial.droppedBytes() == 3);
  CHECK(bench.readTransfer() == "new");
}

TEST_CASE("UsbSerial discards received data while no terminal is open") {
  Bench bench;
  bench.enumerate();
  bench.setDtr(true);
  REQUIRE(bench.sendOut("old"));
  bench.setDtr(false);

  CHECK_FALSE(bench.serial.available());
  CHECK_FALSE(bench.serial.read().has_value());
  // Data without an open terminal is discarded too.
  REQUIRE(bench.sendOut("x"));
  CHECK_FALSE(bench.serial.available());

  bench.setDtr(true);
  CHECK_FALSE(bench.serial.available());
  REQUIRE(bench.sendOut("new"));
  std::array<char, 3> data{};
  REQUIRE(bench.serial.read(art::span<char>(data)).has_value());
  CHECK(std::string(data.begin(), data.end()) == "new");
  CHECK_FALSE(bench.serial.available());
}

TEST_CASE("UsbSerial keeps old data when a session ends between two calls "
          "(known limit, libArT issue 48)") {
  Bench bench;
  bench.enumerate();
  bench.setDtr(true);

  SUBCASE("RX: no read() or available() while the port was closed") {
    REQUIRE(bench.sendOut("old"));
    bench.setDtr(false);
    bench.setDtr(true);
    REQUIRE(bench.sendOut("new"));
    std::array<char, 6> data{};
    REQUIRE(bench.serial.read(art::span<char>(data)).has_value());
    CHECK(std::string(data.begin(), data.end()) == "oldnew");
  }
  SUBCASE("TX: data that the host did not read goes to the next terminal") {
    const std::string old(100, 'o');
    bench.serial.write(bytes(old));
    bench.setDtr(false);
    bench.setDtr(true);
    bench.serial.write(bytes("new"));
    std::string received;
    for (std::string transfer = bench.readTransfer(); !transfer.empty();
         transfer = bench.readTransfer()) {
      received += transfer;
    }
    CHECK(received == old + "new");
  }
}

TEST_CASE("UsbSerial reads what the host sent") {
  Bench bench;
  bench.enumerate();
  bench.setDtr(true);

  CHECK_FALSE(bench.serial.available());
  auto none = bench.serial.read();
  REQUIRE_FALSE(none.has_value());
  CHECK(none.error() == Readable::Timeout);

  REQUIRE(bench.sendOut("abc"));
  CHECK(bench.serial.available());
  auto first = bench.serial.read();
  REQUIRE(first.has_value());
  CHECK(*first == 'a');

  std::array<char, 2> rest{};
  auto count = bench.serial.read(art::span<char>(rest));
  REQUIRE(count.has_value());
  CHECK(*count == 2);
  CHECK(rest == std::array<char, 2>{'b', 'c'});

  CHECK_FALSE(bench.serial.available());
  auto after = bench.serial.read();
  REQUIRE_FALSE(after.has_value());
  CHECK(after.error() == Readable::Timeout);
}

TEST_CASE("UsbSerial reading makes room, so the host can send again") {
  Bench bench;
  bench.enumerate();
  bench.setDtr(true);

  const std::string packet(64, 'p');
  REQUIRE(bench.sendOut(packet));
  REQUIRE(bench.sendOut(packet));
  // The RX queue is full: the endpoint NAKs.
  CHECK_FALSE(bench.sendOut(packet));

  for (size_t i = 0; i < 64; ++i) {
    REQUIRE(bench.serial.read().has_value());
  }
  CHECK(bench.sendOut(packet));
}

TEST_CASE("UsbSerial reports the room and the data that are available") {
  Bench bench;
  CHECK(bench.serial.writeAvailable() == 0);
  CHECK(bench.serial.readAvailable() == 0);

  bench.enumerate();
  CHECK(bench.serial.writeAvailable() == 0); // no terminal yet
  bench.setDtr(true);
  CHECK(bench.serial.writeAvailable() == 128);
  CHECK(bench.serial.isTxIdle());

  bench.serial.write(bytes("hello"));
  CHECK(bench.serial.writeAvailable() == 123);
  CHECK_FALSE(bench.serial.isTxIdle());
  CHECK(bench.readTransfer() == "hello");
  CHECK(bench.serial.isTxIdle());

  REQUIRE(bench.sendOut("abc"));
  CHECK(bench.serial.readAvailable() == 3);
  bench.setDtr(false);
  CHECK(bench.serial.readAvailable() == 0);
}
