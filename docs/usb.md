# USB

## USB device controller

`usb/Stm32UsbDeviceController.h` connects the USB device stack of libArT to
the USB driver of system-stm32. It implements `art::usb::DeviceController`
(libArT) on `hw::UsbDevice` (system-stm32). The class exists only on a device
with the USB full-speed (FS) device driver of system-stm32 (currently
STM32F1, STM32L0 with USB and STM32H5). This driver defines the macro
`SYSTEM_STM32_USBFS`. On other devices, the header and the source compile to
nothing, and the macro `ART_STM32_HAS_USB_DEVICE_CONTROLLER` is not defined.

```cpp
hw::UsbDevice usbHardware;
RingBuffer<hw::usb::Event, 16> usbEvents;
Stm32UsbDeviceController usbController(usbHardware, usbEvents);
art::usb::Device usbDevice(usbController, descriptors);

usbController.begin(); // USB clock must be on (see the system-stm32 USB documentation)
// Do the long initialization here, before start().
usbDevice.start();     // pull-up on: the host starts enumeration
// In the main loop:
usbController.poll();
```

Call `start()` immediately before the main loop starts to call `poll()`.
After `start()`, the host resets the device and sends SETUP packets. If
`poll()` does not run, the events fill the queue.

### Interrupt and main loop

- The USB interrupt only puts the events of `hw::UsbDevice` into the event
  queue. The queue is a single-producer, single-consumer lock-free ring
  buffer.
- `poll()` takes the events from the queue in the main loop and gives them to
  `usb::Device`. Thus all protocol logic runs in the main loop.
- An OUT packet stays in packet memory until `poll()` gives it to
  `usb::Device`, because the endpoint answers NAK until it is armed again.
- The interrupt writes the address of SET_ADDRESS to the hardware when the
  host acknowledges the status stage. The main loop is too slow for this,
  because the host uses the new address 2 ms later.
- After a bus reset, `poll()` drops the events that came before the reset.
- While a new SETUP packet or a bus reset waits in the queue, the controller
  ignores calls on EP0. They belong to the control transfer that the SETUP
  packet or the reset aborted.
- After `closeEndpoint()`, `poll()` drops the queued packet events of that
  endpoint. They do not reach an endpoint that opens again with the same
  address.
- If an OUT packet of an endpoint waits in the queue, `armOutTransfer()` on
  that endpoint takes effect only after `poll()` delivers the packet. Thus
  the next packet cannot overwrite the packet in packet memory.
- `cancelInTransfer()` sets the IN endpoint to NAK with two writes, as
  `closeEndpoint()` does. All interrupts stay masked between the two writes.
  The data toggle does not change. If the host took the packet before the
  call or during the first write, its InComplete event waits in the queue.
  Then `cancelInTransfer()` returns false, and `poll()` delivers the event.
  The call does not touch a stalled endpoint, and it does not take back an
  EP0 packet.

### Double-buffered OUT endpoint

`setDoubleBufferedOut(address)` makes the controller open one
bulk OUT endpoint with two buffers (see the
[system-stm32 USB documentation](https://gitlab.vandertogt.org/embedded-libraries/system-stm32/-/blob/dev/docs/usb.md),
"Double-buffered bulk OUT"). Call it before `begin()`. The hardware then
receives the next packet while the main loop handles the last packet.
Thus the host gets fewer NAKs, and the OUT transfer rate goes up.

```cpp
usbController.setDoubleBufferedOut(0x03);
usbController.begin();
```

The protocol layer sees no difference. It gets one packet for each
`armOutTransfer()`. When the next packet is already in the second buffer,
`hw::UsbDevice::armOut()` puts its event into the queue at once. This
occurs in the main loop, with the USB interrupt masked. Thus the queue
still has one producer at a time. Each endpoint direction still puts at
most one packet event into the queue.

The IN endpoint with the same number cannot open. The packet memory must
have room for two buffers. The STM32F1 has only 512 bytes of packet memory.
There, give `hw::UsbDevice` a smaller buffer descriptor table if necessary
(constructor argument `endpointCount`).

### Queue size

An endpoint answers NAK until it is armed again. Thus each endpoint
direction puts at most one packet event into the queue. Add SETUP packets and
bus events (reset, suspend, resume). A queue of 16 entries is sufficient for
CDC-ACM and MSC together, while the main loop calls `poll()`.
This estimate is not valid when the main loop is blocked: the host retries the
enumeration and sends more SETUP packets and bus resets.

When the queue is full, the interrupt drops the newest event.
`droppedEvents()` counts the dropped events. The controller cannot recover
from a dropped event:

- A dropped OUT or IN event stops that endpoint until the next bus reset or
  SET_CONFIGURATION.
- A dropped bus reset leaves `usb::Device` in its old state, but the hardware
  is in the reset state.

If `droppedEvents()` increases, recover with a new enumeration:

1. Call `usbDevice.stop()`. The host sees a disconnect.
2. Call `usbController.poll()` to empty the queue.
3. Call `usbDevice.start()`. The host enumerates the device again.

### Limits

- `bMaxPacketSize0` must be 64.
- `closeEndpoint()` sets the endpoint to NAK, because the hardware cannot
  disable one endpoint and keep the others. When the last endpoint other than
  EP0 closes, the controller disables all endpoints and frees their packet
  memory.
- Reopening a closed endpoint with a different `wMaxPacketSize` fails while
  another endpoint other than EP0 is open.

## USB serial

`usb/UsbSerial.h` is a `Readable` and a `Writable` on the CDC-ACM function of
libArT (`art::usb::CdcAcmFunction`). It has the same API as `SerialReadable`
and `SerialWritable`. Thus it can be the `LogManager` sink or the stream of a
console.

```cpp
art::usb::CdcAcmFunction<256, 256> cdc(0, art::usb::CdcAcmEndpoints{ 0x81, 16, 0x02, 0x82 });
UsbSerial usbSerial(cdc);
usbDevice.addFunction(cdc);
```

Nothing in `UsbSerial` waits. The USB events arrive only through
`Stm32UsbDeviceController::poll()` in the main loop, so a wait in the main
loop cannot make progress.

- `read()` returns `Readable::Timeout` at once if no byte is in the RX queue.
- `write()` puts the data into the TX queue and drops the data that does not
  fit. While no terminal has the port open, it drops all data. A terminal is
  open when the host sets DTR and the device is configured. `write()` always
  reports all bytes as written, so a logger does not stop on a full or closed
  port. `droppedBytes()` counts the dropped bytes, and `resetDroppedBytes()`
  sets the count to 0.
- `flush()` returns at once. `poll()` sends the TX queue.
- `write()` can cut a message: it sends the start that fits into the TX
  queue and drops the end. A console that must not send a cut message checks
  `writeAvailable()` for the full message size before it writes. If there is
  not enough room, it tries again in a later main loop iteration.
  `isTxIdle()` tells when the host has read all data.
- The `Readable` helpers (`read(span)`, `readNumber()`, `readRLE()`,
  `readUntil()`, `scanf()`) call `read()` for each byte. They stop at the
  first `Timeout`, and the bytes that they took before are lost. Use them only
  for data that is already in the RX queue: check `readAvailable()` first.
  Otherwise, read byte by byte with `read()`.
- Call `UsbSerial` only from the main loop, the context that calls `poll()`.
  The CDC-ACM function does no locking.

### Terminal sessions

A terminal session starts when the host sets DTR. It ends when the host
clears DTR. The CDC-ACM function does not clear its queues at the end of a
session (libArT issue 48 on the GitLab of the maintainers, not public).
`UsbSerial` handles the RX side, but not the TX side:

- RX: while no terminal is open, `read()` and `available()` discard the
  received data and report no data. Call one of them in each main loop
  iteration. Then the data of an ended session does not reach the next
  session. If the next session starts before such a call, its data comes
  after the old data.
- TX: data that `write()` put into the TX queue before the session ended,
  and that the host did not read, stays in the TX queue. The next terminal
  receives this data before the new data.
