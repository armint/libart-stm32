#pragma once

// The controller exists only where system-stm32 has a USB device driver for
// the family (see the system-stm32 README, "USB device driver"). The driver
// defines SYSTEM_STM32_USBFS on such a device. On the others this header and
// its source compile to nothing.
#if __has_include(<UsbDevice.h>)
#include <UsbDevice.h>
#endif

#if __has_include(<UsbDevice.h>) && defined(SYSTEM_STM32_USBFS)
#define ART_STM32_HAS_USB_DEVICE_CONTROLLER 1

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

#include <usb/DeviceController.h>
#include <utils/RingBuffer.h>
#include <utils/compat/span.h>

// startInTransfer() hands its art::span to hw::UsbDevice::startIn(), which
// takes a hw::span. Both must be the same type: std::span with
// ART_USE_STD_EXPECTED_SPAN=1 and C++20 or later for system-stm32, the own
// span of span-compat otherwise. A mixed configuration would convert between
// two span types instead.
static_assert(std::is_same<art::span<const uint8_t>, hw::span<const uint8_t>>::value,
              "art::span (libArT) and hw::span (system-stm32) are different types. "
              "Use ART_USE_STD_EXPECTED_SPAN=ON with a C++20 or later system-stm32 build, "
              "or ART_USE_STD_EXPECTED_SPAN=OFF with a C++17 system-stm32 build.");

/**
 * @brief libArT's usb::DeviceController on system-stm32's hw::UsbDevice.
 *
 * The USB interrupt only queues the events of hw::UsbDevice. poll() takes
 * them from the queue in the main loop and gives them to the event handler
 * (usb::Device). Thus all protocol logic runs in the main loop. The queue is
 * a single-producer, single-consumer lock-free ring buffer: the interrupt
 * pushes, poll() pops.
 *
 * Each queued event is small (hw::usb::Event, 12 bytes). An OUT packet stays
 * in packet memory until poll() delivers it, because the endpoint NAKs until
 * it is armed again. poll() then reads it with hw::UsbDevice::readOut().
 *
 * Work done in the interrupt, because the main loop sees the event too late:
 *  - SET_ADDRESS: setAddress() only stores the address. The interrupt writes
 *    it to the hardware when the host acknowledges the status stage on EP0
 *    IN. A SETUP packet or a bus reset drops a stored address.
 *  - New SETUP: the hardware sets EP0 to NAK. While that SETUP or a bus
 *    reset waits in the queue, the controller ignores startInTransfer(),
 *    armOutTransfer() and setEndpointStall() on EP0. They belong to the
 *    aborted transfer before it.
 *  - Bus reset: hw::UsbDevice resets the endpoints in the interrupt. poll()
 *    drops the events that were queued before the reset, because they refer
 *    to endpoints and transfers that no longer exist.
 *  - closeEndpoint(): poll() drops the packet events of the endpoint that
 *    were queued before the close. They do not reach an endpoint that is
 *    opened again with the same address.
 *  - armOutTransfer() while an OUT packet of that endpoint waits in the
 *    queue: the controller arms the endpoint only after poll() has delivered
 *    the packet, because the next packet would overwrite it in packet memory.
 *
 * Double-buffered OUT: setDoubleBufferedOut() makes the controller open
 * one bulk OUT endpoint with two buffers (hw::UsbDevice). The
 * hardware then receives the next packet while the main loop handles the
 * last one. The protocol layer sees no difference: one packet per
 * armOutTransfer(). hw::UsbDevice::armOut() can report the next packet
 * itself. It then calls the interrupt callback in the main loop, with the
 * USB interrupt masked, so the queue still has one producer at a time.
 *
 * Port limits:
 *  - EP0 must have a bMaxPacketSize0 of 64 (hw::UsbDevice::Ep0MaxPacketSize).
 *  - cancelInTransfer() sets the IN endpoint to NAK with two writes, as
 *    closeEndpoint() does, with all interrupts masked between them. If the
 *    host took the packet before the call or during the first write, its
 *    InComplete event waits in the queue, and the call returns false. The
 *    call does not touch a stalled endpoint.
 *  - cancelInTransfer() does not take back an EP0 packet. The control layer
 *    must not call it on EP0.
 *  - closeEndpoint() sets the endpoint to NAK. The hardware cannot disable one
 *    endpoint and keep the others. When the last endpoint other than EP0 is
 *    closed, the controller disables all of them and frees their packet
 *    memory. Reopening a closed endpoint with another maxPacketSize fails
 *    while another endpoint other than EP0 is open.
 *
 * Queue size: an endpoint NAKs until it is armed again, so each endpoint
 * direction queues at most one Out or InComplete event. Add SETUP packets and
 * bus events (reset, suspend, resume). 16 entries are enough for CDC-ACM and
 * MSC together, while the main loop runs poll(). droppedEvents() counts
 * the events that did not fit: the newest event is dropped. A dropped event
 * cannot be recovered without a new enumeration (see docs/usb.md, "Queue size").
 *
 * Threading: call every function except the interrupt callback from the main
 * loop, the same context that runs poll().
 */
class Stm32UsbDeviceController final : public art::usb::DeviceController {
   public:
   using Event = hw::usb::Event;

   /**
    * @param device The USB peripheral driver.
    * @param eventQueue The queue between the interrupt and poll(). Used by this
    * controller only.
    */
   Stm32UsbDeviceController(hw::UsbDevice &device, RingBufferBase<Event> &eventQueue);

   Stm32UsbDeviceController(const Stm32UsbDeviceController &) = delete;
   Stm32UsbDeviceController &operator=(const Stm32UsbDeviceController &) = delete;

   /**
    * @brief Clears the queue and starts the peripheral (hw::UsbDevice::begin()).
    * The device stays disconnected until connect(). The USB clock must be on.
    */
   void begin();

   /// Stops the peripheral (hw::UsbDevice::end()). The host sees a disconnect.
   void end();

   /// Gives the queued events to the event handler. Call it from the main loop.
   void poll();

   /**
    * @brief Opens the bulk OUT endpoint endpointAddress double-buffered
    * (see the class comment) when the protocol layer opens it. Call it
    * before begin(). The IN endpoint with the same number cannot open.
    * Only one endpoint; a second call replaces the first.
    */
   void setDoubleBufferedOut(uint8_t endpointAddress) {
      doubleBufferedOut_ = endpointAddress;
   }

   /// The number of events that the interrupt could not queue because the queue was full.
   [[nodiscard]] uint32_t droppedEvents() const {
      return droppedEvents_.load(std::memory_order_relaxed);
   }

   // art::usb::DeviceController
   void setEventHandler(art::usb::ControllerEvents *handler) override;
   void connect() override;
   void disconnect() override;
   void setAddress(uint8_t address) override;
   bool openEndpoint(const art::usb::EndpointConfig &config) override;
   void closeEndpoint(uint8_t endpointAddress) override;
   void setEndpointStall(uint8_t endpointAddress, bool stall) override;
   bool startInTransfer(uint8_t endpointAddress, art::span<const uint8_t> packet) override;
   bool cancelInTransfer(uint8_t endpointAddress) override;
   void armOutTransfer(uint8_t endpointAddress) override;

   private:
   static constexpr uint8_t kNoAddress = 0xFFu;

   hw::UsbDevice &device_;
   RingBufferBase<Event> &eventQueue_;
   art::usb::ControllerEvents *handler_ = nullptr;

   // Open endpoints as the protocol layer sees them, one bit per endpoint
   // number. Main loop only.
   uint8_t openIn_ = 0u;
   uint8_t openOut_ = 0u;
   // The OUT endpoint of setDoubleBufferedOut(), or kNoAddress
   uint8_t doubleBufferedOut_ = kNoAddress;

   // Shared with the interrupt. Only loads and stores: Cortex-M0+ has no
   // atomic read-modify-write. The *Queued_ counters are written by the
   // interrupt callback (also when hw::UsbDevice::armOut() calls it with
   // the interrupt masked), the *Delivered_ counters by poll(). A pair
   // differs while an event of that type waits in the queue.
   std::atomic<uint8_t> pendingAddress_{ kNoAddress };
   std::atomic<uint8_t> resetsQueued_{ 0u };
   std::atomic<uint8_t> setupsQueued_{ 0u };
   uint8_t resetsDelivered_ = 0u;
   uint8_t setupsDelivered_ = 0u;
   std::atomic<uint32_t> droppedEvents_{ 0u };
   // Out and InComplete events per endpoint direction (OUT 0..7, IN 8..15).
   std::array<std::atomic<uint8_t>, 2u * hw::UsbDevice::EndpointCount> packetsQueued_{};
   std::array<uint8_t, 2u * hw::UsbDevice::EndpointCount> packetsDelivered_{};

   // Main loop only. staleEvents_: packet events still queued when the
   // endpoint was closed; poll() drops them. deferredArms_: OUT endpoints
   // (bit per number) that armOutTransfer() arms after poll() has delivered
   // their queued packet.
   std::array<uint8_t, 2u * hw::UsbDevice::EndpointCount> staleEvents_{};
   uint8_t deferredArms_ = 0u;

   static void onInterruptEvent(void *context, const Event &event);
   void handleInterruptEvent(const Event &event);
   void deliver(const Event &event);
   [[nodiscard]] bool isSupersededControlCall(uint8_t endpointAddress) const;
   [[nodiscard]] bool isOpen(uint8_t endpointAddress) const;
   [[nodiscard]] bool isOutQueued(uint8_t endpointAddress) const;
   void armDeferredOut(uint8_t endpointAddress);
};

#endif
