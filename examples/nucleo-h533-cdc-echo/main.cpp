// CDC echo image for the NUCLEO-H533RE. It checks the USB device controller
// and the serial adapter of this library on the STM32H5.
//
// - USB (CN3, USB Type-C): a CDC-ACM device (virtual COM port, pid.codes
//   test ID 1209:0001). It echoes every byte that it receives. Open the port
//   in a terminal program: UsbSerial discards data while DTR is low.
// - USART2 (PA2/PA3, the ST-LINK virtual COM port, 115200 8N1): Serial,
//   interrupt-driven. It prints the USB state changes and echoes every byte
//   that it receives.
//
// It sets 250 MHz from the 24 MHz HSE crystal. Hold B1 (PC13) during reset
// to use HSI instead. The USB clock is HSI48, trimmed by the CRS from the USB
// start-of-frame packets. LD2 (PA5) blinks at 1 Hz while the main loop runs.
#include <cmsis_device.h>

#include <algorithm>
#include <array>

#include <Serial.h>
#include <usb/Stm32UsbDeviceController.h>
#include <usb/UsbSerial.h>

#include <peripherals/Crs.h>
#include <peripherals/GpioPin.h>
#include <peripherals/Rcc.h>
#include <peripherals/Uid.h>
#include <usb/DescriptorBuilder.h>
#include <usb/Device.h>
#include <utils/Interrupt.h>
#include <utils/RingBuffer.h>
#include <utils/Timekeeping.h>
#include <utils/timestamp.h>

static_assert(ART_STM32_HAS_USB_DEVICE_CONTROLLER && ART_STM32_HAS_USB_SERIAL, "the H5 needs the USB adapter");
static_assert(HSE_VALUE == 24000000UL, "the NUCLEO-H533RE has a 24 MHz crystal (SYSTEM_STM32_HSE_VALUE)");

namespace {

using namespace art::usb::descriptor;
using Pin = hw::GpioPin;

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
constexpr auto ProductDescriptor = stringDescriptor(u"libart-stm32 NUCLEO-H533RE CDC echo");

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

RingBuffer<uint8_t, 256> vcpTx;
RingBuffer<uint8_t, 64> vcpRx;
Serial vcp(USART2, vcpTx, vcpRx);

void alternate(GPIO_TypeDef *port, uint8_t pin, Pin::AlternateFunction af, Pin::Pull pull = Pin::Pull::None) {
   const Pin p(port, pin);
   p.setAlternateFunction(af);
   p.configure(Pin::Mode::Alternate, Pin::OutputType::PushPull, pull, Pin::Speed::VeryHigh);
}

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

void startVcp() {
   Rcc::enableUSART2Clock();
   alternate(GPIOA, 2, Pin::AlternateFunction::AF7);
   alternate(GPIOA, 3, Pin::AlternateFunction::AF7, Pin::Pull::Up);
   auto &uart = vcp.uart();
   uart.configure(115200, Usart::Parity::None, Usart::StopBits::StopBits_1);
   uart.enableTx();
   uart.enableRx();
   uart.enableRxNotEmptyInterrupt();
   uart.enable();
   Interrupt::connectMemberHandler<Serial, &Serial::handleInterrupt>(USART2_IRQn, vcp);
   NVIC_EnableIRQ(USART2_IRQn);
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

void echoVcp() {
   while(vcp.available()) {
      auto byte = vcp.read();
      if(!byte) {
         break;
      }
      const auto c = static_cast<uint8_t>(*byte);
      (void)vcp.write(art::span<const uint8_t>(&c, 1u));
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
      (void)vcp.printf(terminalOpen ? "USB terminal open (DTR set)\r\n" : "USB terminal closed (DTR clear)\r\n");
   }
   if(usbController.droppedEvents() != droppedEvents) {
      droppedEvents = usbController.droppedEvents();
      (void)vcp.printf("USB events dropped: %lu\r\n", static_cast<unsigned long>(droppedEvents));
   }
}

} // namespace

// The platform hook of libArT (utils/timestamp.h). SysTick runs at 1 kHz.
timestamp_t timestamp_in_ms() {
   return getUptimeTicks();
}

int main() {
   Rcc::enableGPIOAClock();
   Rcc::enableGPIOCClock();
   const Pin led(GPIOA, 5);
   led.configure(Pin::Mode::Output, Pin::OutputType::PushPull);
   const Pin key(GPIOC, 13);
   key.configure(Pin::Mode::Input);

   const bool clockOk = key.read() ? Rcc::setClock250MhzHSI() : Rcc::setClock250MhzHSE();
   Rcc::enableICache();
   SysTick_Config(SystemCoreClock / 1000u);

   startVcp();
   (void)vcp.printf("\r\nlibart-stm32 NUCLEO-H533RE CDC echo, %lu Hz%s\r\n", static_cast<unsigned long>(SystemCoreClock),
                    clockOk ? "" : " (clock recipe failed)");

   std::array<uint8_t, hw::Uid::SizeBytes> uid{};
   hw::Uid::read(uid);
   serialNumber.set(uid);

   if(!usbDevice.addFunction(cdcFunction) || !startUsbClock()) {
      (void)vcp.printf("USB setup failed\r\n");
   } else {
      usbController.begin();
      usbDevice.start();
      (void)vcp.printf("USB started: %04x:%04x\r\n", VendorId, ProductId);
   }

   timestamp_t nextBlink = getUptimeTicks() + 500u;
   for(;;) {
      usbController.poll();
      echoUsb();
      echoVcp();
      reportUsbState();
      if(getUptimeTicks() >= nextBlink) {
         nextBlink += 500u;
         led.write(!led.read());
      }
   }
}
