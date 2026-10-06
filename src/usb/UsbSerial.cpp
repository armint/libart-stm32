#include "UsbSerial.h"

#if defined(ART_STM32_HAS_USB_SERIAL)

#include <array>

UsbSerial::UsbSerial(art::usb::CdcAcmFunctionBase &function)
   : function_(function) {
}

art::expected<char, Readable::Error> UsbSerial::read() {
   uint8_t byte = 0u;
   if(!acceptsInput() || function_.read(art::span<uint8_t>(&byte, 1u)) == 0u) {
      return art::unexpected(Readable::Timeout);
   }
   return static_cast<char>(byte);
}

bool UsbSerial::available() {
   return acceptsInput() && function_.readAvailable() > 0u;
}

bool UsbSerial::acceptsInput() {
   if(function_.isTerminalOpen()) {
      return true;
   }
   // The data is from a terminal session that has ended. Discard it, so that
   // it does not reach the next session. The discard also lets the function
   // arm its OUT endpoint again.
   std::array<uint8_t, art::usb::CdcAcmFunctionBase::kDataPacketSize> scratch{};
   while(function_.read(scratch) != 0u) {
   }
   return false;
}

art::expected<size_t, Writable::Error> UsbSerial::write(art::span<const uint8_t> data) {
   // isTerminalOpen() is false while the function is not configured.
   const size_t taken = function_.isTerminalOpen() ? function_.write(data) : 0u;
   droppedBytes_ += static_cast<uint32_t>(data.size() - taken);
   return data.size();
}

art::expected<void, Writable::Error> UsbSerial::flush() {
   return {};
}

#endif
