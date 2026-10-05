# LibArt-STM32

LibArT implementations specifically for stm32 targets

## USB device controller

`usb/Stm32UsbDeviceController.h` connects the USB device stack of libArT to
the USB driver of system-stm32. It implements `art::usb::DeviceController`
(libArT) on `hw::UsbDevice` (system-stm32). The class exists only on a family
that has a system-stm32 USB driver (currently STM32L0 with USB). On other
families, the header and the source compile to nothing, and the macro
`ART_STM32_HAS_USB_DEVICE_CONTROLLER` is not defined.

```cpp
hw::UsbDevice usbHardware;
RingBuffer<hw::usb::Event, 16> usbEvents;
Stm32UsbDeviceController usbController(usbHardware, usbEvents);
art::usb::Device usbDevice(usbController, descriptors);

usbController.begin(); // USB clock must be on (see system-stm32 README)
usbDevice.start();     // pull-up on
// In the main loop:
usbController.poll();
```

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

### Queue size

An endpoint answers NAK until it is armed again. Thus each endpoint
direction puts at most one packet event into the queue. Add SETUP packets and
bus events (reset, suspend, resume). A queue of 16 entries is sufficient for
CDC-ACM and MSC together. `droppedEvents()` counts the events that did not fit
into the queue.

### Limits

- `bMaxPacketSize0` must be 64.
- `closeEndpoint()` sets the endpoint to NAK, because the hardware cannot
  disable one endpoint and keep the others. When the last endpoint other than
  EP0 closes, the controller disables all endpoints and frees their packet
  memory.
- Reopening a closed endpoint with a different `wMaxPacketSize` fails while
  another endpoint other than EP0 is open.

## Host tests

`tests/` contains host tests with doctest. Stubs in `tests/stubs` replace the
hardware and some libArT classes. The USB controller tests use the real
libArT USB headers and `usb::Device`. They find libArT in `../libArT`, next to
this repository. Set `LIBART_DIR` to use another libArT checkout:

```sh
cmake -S tests -B build-tests -D LIBART_DIR=/path/to/libArT
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```
