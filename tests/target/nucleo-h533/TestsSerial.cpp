#include "Board.h"
#include "Report.h"
#include "Tests.h"

#include <BlockingSerialWritable.h>
#include <Serial.h>
#include <utils/Interrupt.h>
#include <utils/RingBuffer.h>

namespace hwtest {
namespace {

constexpr uint32_t Baud = 1000000;
constexpr const char *SerialWire = "needs the wire PA9-PA10";

RingBuffer<uint8_t, 256> serialTx;
RingBuffer<uint8_t, 256> serialRx;
Serial serial1(USART1, serialTx, serialRx);

void drainRx(const hw::Usart &uart) {
   while(uart.isRxRegisterNotEmpty()) {
      (void)uart.getData();
   }
   USART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
}

// Serial, interrupt-driven, without the FIFO: 200 bytes out and back in.
bool serialLoopback() {
   auto &uart = serial1.uart();
   uart.disable();
   uart.configure(Baud);
   uart.enableTx();
   uart.enableRx();
   uart.enableRxNotEmptyInterrupt();
   uart.enable();
   Interrupt::connectMemberHandler<Serial, &Serial::handleInterrupt>(USART1_IRQn, serial1);
   NVIC_EnableIRQ(USART1_IRQn);
   serial1.clear();

   uint8_t data[200];
   for(size_t i = 0; i < sizeof(data); ++i) {
      data[i] = static_cast<uint8_t>(i * 31 + 7);
   }
   bool ok = serial1.write(art::span<const uint8_t>(data, sizeof(data))).has_value() && serial1.flush().has_value();
   size_t received = 0;
   bool same = true;
   ok = ok && waitFor(
                 [&] {
                    while(serial1.available()) {
                       const auto byte = serial1.read();
                       if(!byte) {
                          break;
                       }
                       same = same && received < sizeof(data) && static_cast<uint8_t>(*byte) == data[received];
                       ++received;
                    }
                    return received >= sizeof(data);
                 },
                 20000);
   (void)vcp.printf("  serial usart1: %u of %u bytes back\r\n", static_cast<unsigned>(received),
                    static_cast<unsigned>(sizeof(data)));

   NVIC_DisableIRQ(USART1_IRQn);
   Interrupt::releaseHandler(USART1_IRQn);
   uart.disableRxNotEmptyInterrupt();
   uart.disable();
   return ok && same && received == sizeof(data);
}

// BlockingSerialWritable sends 8 bytes; the RX FIFO of USART1 keeps them.
bool blockingWritable() {
   const hw::Usart uart(USART1);
   uart.disable();
   uart.enableFifo();
   uart.enableRx();
   BlockingSerialWritable writer(USART1);
   writer.initialize(Baud);
   (void)waitFor([&] { return uart.isTransferComplete(); }, 1000); // idle frame
   drainRx(uart);

   const uint8_t data[8] = { 'h', '5', '-', 't', 'e', 's', 't', '\n' };
   bool ok = writer.write(art::span<const uint8_t>(data, sizeof(data))).has_value() && writer.flush().has_value();
   size_t received = 0;
   while(ok && uart.isRxRegisterNotEmpty()) {
      ok = received < sizeof(data) && uart.getData() == data[received];
      ++received;
   }
   uart.disable();
   uart.disableFifo();
   return ok && received == sizeof(data);
}

} // namespace

void testSerial() {
   check("libart-serial-usart1-loopback", serialLoopback(), SerialWire);
   check("libart-blocking-serial-writable", blockingWritable(), SerialWire);
}

} // namespace hwtest
