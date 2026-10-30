# Serial port

`Serial` uses ring buffers that the application owns. Set the system clock
first, because `configure()` calculates the baud rate from the USART clock.
Then do these steps:

1. Enable the clock of the USART.
2. Configure the pins and the USART.
3. Connect the USART interrupt to `Serial::handleInterrupt()`.

```cpp
#include <Serial.h>
#include <peripherals/Rcc.h>
#include <utils/Interrupt.h>
#include <utils/RingBuffer.h>

RingBuffer<uint8_t, 64> txBuffer;
RingBuffer<uint8_t, 128> rxBuffer;
Serial serial(USART2, txBuffer, rxBuffer);

void initSerial() {
   Rcc::enableUSART2Clock();
   // Configure the TX and RX pins as alternate function (hw::GpioPin).
   auto &uart = serial.uart();
   uart.configure(115200, Usart::Parity::None, Usart::StopBits::StopBits_1);
   uart.enableTx();
   uart.enableRx();
   uart.enableRxNotEmptyInterrupt();
   uart.enable();

   Interrupt::connectMemberHandler<Serial, &Serial::handleInterrupt>(USART2_IRQn, serial);
   NVIC_EnableIRQ(USART2_IRQn);
}
```

Then use `serial` as any libArT `Readable` or `Writable`, for example as the
`LogManager` sink. For the USB serial port, see [USB serial](usb.md#usb-serial).
