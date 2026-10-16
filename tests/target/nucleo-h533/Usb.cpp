#include "Usb.h"

#include "Board.h"
#include "Report.h"

#include <algorithm>
#include <array>

#include <usb/Stm32UsbDeviceController.h>
#include <usb/UsbSerial.h>

#include <peripherals/Crs.h>
#include <peripherals/Rcc.h>
#include <peripherals/Uid.h>
#include <usb/DescriptorBuilder.h>
#include <usb/Device.h>
#include <utils/RingBuffer.h>
#include <utils/Timekeeping.h>

static_assert(ART_STM32_HAS_USB_DEVICE_CONTROLLER && ART_STM32_HAS_USB_SERIAL, "the H5 needs the USB adapter");

namespace hwtest {
namespace {

using namespace art::usb::descriptor;

// pid.codes test VID/PID: for tests only, not for products.
constexpr uint16_t VendorId = 0x1209u;
constexpr uint16_t ProductId = 0x0001u;

constexpr uint8_t ControlInterface = 0u;
constexpr uint8_t DataInterface = 1u;
constexpr art::usb::CdcAcmEndpoints Endpoints{};

constexpr uint8_t StringProduct = 1u;
constexpr uint8_t StringSerialNumber = 2u;

// Miscellaneous/common/IAD device class: Windows then binds usbser.sys to the
// two CDC interfaces of the IAD.
constexpr auto DeviceDescriptor = device(DeviceFields{
   .bDeviceClass = classcode::kMiscellaneous,
   .bDeviceSubClass = classcode::kMiscellaneousSubclassCommon,
   .bDeviceProtocol = classcode::kMiscellaneousProtocolIad,
   .idVendor = VendorId,
   .idProduct = ProductId,
   .iProduct = StringProduct,
   .iSerialNumber = StringSerialNumber,
});

constexpr auto ConfigurationDescriptor = configuration(
   ConfigurationFields{ .selfPowered = true, .maxPowerMilliamps = 100u },
   interfaceAssociation(ControlInterface, 2, classcode::kCdc, cdc::kSubclassAcm, 0),
   interfaceDescriptor(InterfaceFields{ ControlInterface, 0, classcode::kCdc, cdc::kSubclassAcm, 0, 0 },
                       cdcHeader(),
                       cdcCallManagement(0, DataInterface),
                       cdcAcm(),
                       cdcUnion(ControlInterface, DataInterface),
                       endpoint(Endpoints.notification, art::usb::EndpointType::Interrupt, Endpoints.notificationMaxPacketSize, 16)),
   interfaceDescriptor(InterfaceFields{ DataInterface, 0, classcode::kCdcData, 0, 0, 0 },
                       endpoint(Endpoints.dataOut, art::usb::EndpointType::Bulk, art::usb::CdcAcmFunctionBase::kDataPacketSize),
                       endpoint(Endpoints.dataIn, art::usb::EndpointType::Bulk, art::usb::CdcAcmFunctionBase::kDataPacketSize)));

constexpr auto LanguageDescriptor = languages(kLanguageEnglishUs);
constexpr auto ProductDescriptor = stringDescriptor(u"libart-stm32 NUCLEO-H533RE hardware test");

SerialNumber<hw::Uid::SizeBytes> serialNumber;

const std::array<art::span<const uint8_t>, 3> strings{
   asSpan(LanguageDescriptor),
   asSpan(ProductDescriptor),
   serialNumber.bytes(),
};

const art::usb::Descriptors descriptors{
   .device = asSpan(DeviceDescriptor),
   .configuration = asSpan(ConfigurationDescriptor),
   .strings = art::span<const art::span<const uint8_t>>(strings.data(), strings.size()),
};

hw::UsbDevice usbHardware;
RingBuffer<hw::usb::Event, 16> usbEvents;
Stm32UsbDeviceController usbController(usbHardware, usbEvents);
art::usb::Device usbDevice(usbController, descriptors);
art::usb::CdcAcmFunction<256, 256> cdcFunction(ControlInterface, Endpoints);
UsbSerial usbSerial(cdcFunction);

// HSI48 trimmed by the CRS from USB SOF, PA11/PA12 (USB_DM/USB_DP, AF10).
bool startUsbClock() {
   if(!Rcc::enableHSI48()) {
      return false;
   }
   Rcc::setUSBClockSource(Rcc::USBClockSource::HSI48);
   Rcc::enableCRSClock();
   if(!hw::Crs::configure({})) {
      return false;
   }
   hw::Crs::enable();
   alternate(GPIOA, 11, Pin::AlternateFunction::AF10);
   alternate(GPIOA, 12, Pin::AlternateFunction::AF10);
   Rcc::enableUSBClock();
   return true;
}

void printUid(const std::array<uint8_t, hw::Uid::SizeBytes> &uid) {
   char text[2 * hw::Uid::SizeBytes + 1];
   constexpr char digits[] = "0123456789ABCDEF";
   for(size_t i = 0; i < uid.size(); ++i) {
      text[2 * i] = digits[uid[i] >> 4];
      text[2 * i + 1] = digits[uid[i] & 0x0F];
   }
   text[sizeof(text) - 1] = '\0';
   (void)vcp.printf("UID %s\r\n", text);
}

// Takes only what fits into the TX queue, so no byte is lost. Each chunk
// goes to write() in one call, so the IN packets keep the OUT packet sizes.
void echoUsb() {
   size_t count = std::min(usbSerial.readAvailable(), usbSerial.writeAvailable());
   while(count > 0u) {
      std::array<uint8_t, art::usb::CdcAcmFunctionBase::kDataPacketSize> chunk{};
      size_t length = 0u;
      while(length < std::min(count, chunk.size())) {
         auto byte = usbSerial.read();
         if(!byte) {
            break;
         }
         chunk[length++] = static_cast<uint8_t>(*byte);
      }
      if(length == 0u) {
         break;
      }
      (void)usbSerial.write(art::span<const uint8_t>(chunk.data(), length));
      count -= length;
   }
}

// The CRS flags are sticky: clear them 200 ms after the configuration, then
// read the status 300 ms later. The host sends a SOF every 1 ms.
void checkCrs(bool configured) {
   enum class State { WaitConfigured, WaitClear, WaitStatus, Done };
   static State state = State::WaitConfigured;
   static timestamp_t due = 0;
   const timestamp_t now = getUptimeTicks();
   switch(state) {
   case State::WaitConfigured:
      if(configured) {
         due = now + 200U;
         state = State::WaitClear;
      }
      break;
   case State::WaitClear:
      if(now >= due) {
         hw::Crs::clearFlags();
         due = now + 300U;
         state = State::WaitStatus;
      }
      break;
   case State::WaitStatus:
      if(now >= due) {
         (void)vcp.printf("  crs trim %lu\r\n", static_cast<unsigned long>(hw::Crs::getTrim()));
         check("crs-sync-usb-sof", hw::Crs::getSyncStatus() == hw::Crs::SyncStatus::Ok);
         state = State::Done;
      }
      break;
   case State::Done:
      break;
   }
}

void reportUsbState() {
   static bool configured = false;
   static bool terminalOpen = false;
   static uint32_t droppedEvents = 0u;
   if(usbDevice.isConfigured() != configured) {
      configured = usbDevice.isConfigured();
      (void)vcp.printf(configured ? "USB configured\r\n" : "USB not configured\r\n");
   }
   if(usbSerial.isTerminalOpen() != terminalOpen) {
      terminalOpen = usbSerial.isTerminalOpen();
      (void)vcp.printf(terminalOpen ? "USB terminal open\r\n" : "USB terminal closed\r\n");
   }
   if(usbController.droppedEvents() != droppedEvents) {
      droppedEvents = usbController.droppedEvents();
      (void)vcp.printf("USB events dropped: %lu\r\n", static_cast<unsigned long>(droppedEvents));
   }
   checkCrs(configured);
}

} // namespace

bool startUsb() {
   std::array<uint8_t, hw::Uid::SizeBytes> uid{};
   hw::Uid::read(uid);
   serialNumber.set(uid);
   printUid(uid);
   if(!usbDevice.addFunction(cdcFunction) || !startUsbClock()) {
      return false;
   }
   usbController.begin();
   usbDevice.start();
   return true;
}

void pollUsb() {
   usbController.poll();
   echoUsb();
   reportUsbState();
}

} // namespace hwtest
