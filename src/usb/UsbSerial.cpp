#include "UsbSerial.h"

#if defined(ART_STM32_HAS_USB_SERIAL)

UsbSerial::UsbSerial(art::usb::CdcAcmFunctionBase &function)
   : function_(function) {
}

std::expected<char, Readable::Error> UsbSerial::read() {
   uint8_t byte = 0u;
   if(function_.read(std::span<uint8_t>(&byte, 1u)) == 0u) {
      return std::unexpected(Readable::Timeout);
   }
   return static_cast<char>(byte);
}

bool UsbSerial::available() {
   return function_.readAvailable() > 0u;
}

std::expected<size_t, Writable::Error> UsbSerial::write(std::span<const uint8_t> data) {
   // isTerminalOpen() is false while the function is not configured.
   const size_t taken = function_.isTerminalOpen() ? function_.write(data) : 0u;
   droppedBytes_ += static_cast<uint32_t>(data.size() - taken);
   return data.size();
}

std::expected<void, Writable::Error> UsbSerial::flush() {
   return {};
}

#endif
