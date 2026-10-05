#pragma once

// UsbSerial needs the CDC-ACM function of libArT. With an older libArT, this
// header and its source compile to nothing.
#if __has_include(<usb/CdcAcmFunction.h>)
#define ART_STM32_HAS_USB_SERIAL 1

#include <cstdint>
#include <expected>
#include <span>

#include <streams/Readable.h>
#include <streams/Writable.h>
#include <usb/CdcAcmFunction.h>

/**
 * @brief A Readable and Writable on a USB CDC-ACM function (virtual COM
 * port), with the same API as SerialReadable and SerialWritable.
 *
 * It can be the LogManager sink or the stream of a console. The data goes
 * through the RX and TX queues of art::usb::CdcAcmFunction.
 *
 * Nothing waits: the USB events arrive only through
 * Stm32UsbDeviceController::poll() in the main loop, so a wait in the main
 * loop could not make progress.
 *  - read() returns Readable::Timeout at once when no byte was received.
 *  - write() takes what fits into the TX queue and drops the rest. It drops
 *    everything while no terminal has the port open (DTR low, or the device
 *    is not configured). droppedBytes() counts the dropped bytes. write()
 *    reports all bytes as written, so that a logger never stops on a full or
 *    closed port.
 *
 * Terminal sessions: a session starts when the host sets DTR and ends when
 * it clears DTR. The CDC-ACM function does not clear its queues at the end of
 * a session (libArT issue embedded-libraries/libart#48). Thus:
 *  - RX: while no terminal is open, read() and available() discard the
 *    received data and report none. Call one of them in each main loop
 *    iteration, so that data of an ended session does not reach the next
 *    one. If the next session starts before such a call, its data comes
 *    after the old data.
 *  - TX: data that write() queued before the session ended, and that the
 *    host did not read, stays in the TX queue. The next terminal receives
 *    it before the new data.
 *  - flush() returns at once. poll() sends the TX queue.
 *
 * Threading: call it only from the main loop, the context that runs poll().
 * The CDC-ACM function does no locking.
 */
class UsbSerial : public Readable, public Writable {
   public:
   explicit UsbSerial(art::usb::CdcAcmFunctionBase &function);

   using Readable::read;
   using Writable::write;

   // Readable
   std::expected<char, Readable::Error> read() override;
   bool available() override;

   // Writable
   std::expected<size_t, Writable::Error> write(std::span<const uint8_t> data) override;
   std::expected<void, Writable::Error> flush() override;

   /// True if a terminal program on the host has the port open (DTR set).
   [[nodiscard]] bool isTerminalOpen() const {
      return function_.isTerminalOpen();
   }

   /// The number of bytes that write() dropped since construction or resetDroppedBytes().
   [[nodiscard]] uint32_t droppedBytes() const {
      return droppedBytes_;
   }

   void resetDroppedBytes() {
      droppedBytes_ = 0u;
   }

   private:
   /// True if a terminal is open. Otherwise it discards the RX queue.
   bool acceptsInput();

   art::usb::CdcAcmFunctionBase &function_;
   uint32_t droppedBytes_ = 0u;
};

#endif
