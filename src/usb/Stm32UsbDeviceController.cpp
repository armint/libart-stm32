#include "Stm32UsbDeviceController.h"

#if defined(ART_STM32_HAS_USB_DEVICE_CONTROLLER)

#include <array>

namespace {

using art::usb::endpointNumber;
using art::usb::isInEndpoint;

constexpr uint8_t kEndpoint0In = 0x80u;
constexpr uint8_t kEndpoint0Bit = 0x01u;

hw::usb::EndpointType toHardware(art::usb::EndpointType type) {
   switch(type) {
   case art::usb::EndpointType::Control:
      return hw::usb::EndpointType::Control;
   case art::usb::EndpointType::Isochronous:
      return hw::usb::EndpointType::Isochronous;
   case art::usb::EndpointType::Bulk:
      return hw::usb::EndpointType::Bulk;
   case art::usb::EndpointType::Interrupt:
      break;
   }
   return hw::usb::EndpointType::Interrupt;
}

// One more store after a load: the interrupt is the only writer of these counters.
template <typename T>
void increment(std::atomic<T> &counter, std::memory_order order) {
   counter.store(static_cast<T>(counter.load(std::memory_order_relaxed) + 1u), order);
}

} // namespace

Stm32UsbDeviceController::Stm32UsbDeviceController(hw::UsbDevice &device, RingBufferBase<Event> &eventQueue)
   : device_(device)
   , eventQueue_(eventQueue) {
}

void Stm32UsbDeviceController::begin() {
   // The interrupt is not connected yet, so the consumer may clear the queue.
   eventQueue_.clear();
   resetsQueued_.store(0u, std::memory_order_relaxed);
   setupsQueued_.store(0u, std::memory_order_relaxed);
   resetsDelivered_ = 0u;
   setupsDelivered_ = 0u;
   pendingAddress_.store(kNoAddress, std::memory_order_relaxed);
   openIn_ = 0u;
   openOut_ = 0u;
   device_.begin(&Stm32UsbDeviceController::onInterruptEvent, this);
}

void Stm32UsbDeviceController::end() {
   device_.end();
}

void Stm32UsbDeviceController::poll() {
   Event event{};
   while(eventQueue_.try_pop(event)) {
      deliver(event);
   }
}

void Stm32UsbDeviceController::onInterruptEvent(void *context, const Event &event) {
   static_cast<Stm32UsbDeviceController *>(context)->handleInterruptEvent(event);
}

void Stm32UsbDeviceController::handleInterruptEvent(const Event &event) {
   switch(event.type) {
   case hw::usb::EventType::Reset:
   case hw::usb::EventType::Setup:
      // The host aborted a SET_ADDRESS whose status stage did not complete.
      pendingAddress_.store(kNoAddress, std::memory_order_relaxed);
      break;
   case hw::usb::EventType::InComplete:
      // The status stage of SET_ADDRESS is complete. The host uses the new
      // address 2 ms later, so this cannot wait for the main loop.
      if(event.endpoint == kEndpoint0In) {
         const uint8_t address = pendingAddress_.load(std::memory_order_relaxed);
         if(address != kNoAddress) {
            device_.setAddress(address);
            pendingAddress_.store(kNoAddress, std::memory_order_relaxed);
         }
      }
      break;
   default:
      break;
   }

   if(!eventQueue_.try_push(event)) {
      increment(droppedEvents_, std::memory_order_relaxed);
      return;
   }
   if(event.type == hw::usb::EventType::Reset) {
      increment(resetsQueued_, std::memory_order_release);
   } else if(event.type == hw::usb::EventType::Setup) {
      increment(setupsQueued_, std::memory_order_release);
   }
}

void Stm32UsbDeviceController::deliver(const Event &event) {
   // FIFO order: while a reset waits in the queue, every event before it
   // belongs to the endpoints and transfers that the reset ended.
   const bool beforeReset = resetsDelivered_ != resetsQueued_.load(std::memory_order_acquire);

   if(event.type == hw::usb::EventType::Reset) {
      ++resetsDelivered_;
      // hw::UsbDevice has closed every endpoint and reopened EP0 for the
      // protocol layer to open again.
      openIn_ = 0u;
      openOut_ = 0u;
      if(handler_ != nullptr) {
         handler_->busReset();
      }
      return;
   }
   if(event.type == hw::usb::EventType::Setup) {
      ++setupsDelivered_;
   }
   if(beforeReset || handler_ == nullptr) {
      return;
   }

   switch(event.type) {
   case hw::usb::EventType::Setup:
      handler_->setupReceived(art::usb::SetupPacket::fromBytes(event.setup));
      break;
   case hw::usb::EventType::Out:
      if(isOpen(event.endpoint)) {
         std::array<uint8_t, hw::UsbDevice::Ep0MaxPacketSize> packet{};
         const uint16_t size = device_.readOut(event.endpoint, packet);
         handler_->outPacketReceived(event.endpoint, art::span<const uint8_t>(packet.data(), size));
      }
      break;
   case hw::usb::EventType::InComplete:
      if(isOpen(event.endpoint)) {
         handler_->inPacketSent(event.endpoint);
      }
      break;
   case hw::usb::EventType::Suspend:
      handler_->suspended();
      break;
   case hw::usb::EventType::Resume:
      handler_->resumed();
      break;
   case hw::usb::EventType::Reset:
      break;
   }
}

void Stm32UsbDeviceController::setEventHandler(art::usb::ControllerEvents *handler) {
   handler_ = handler;
}

void Stm32UsbDeviceController::connect() {
   device_.connect();
}

void Stm32UsbDeviceController::disconnect() {
   device_.disconnect();
}

void Stm32UsbDeviceController::setAddress(uint8_t address) {
   pendingAddress_.store(address, std::memory_order_relaxed);
   // A SETUP or a reset that the interrupt queued before the store above
   // already aborted this SET_ADDRESS, and the interrupt could not drop the
   // address yet. One that comes after the store drops it in the interrupt.
   if(setupsQueued_.load(std::memory_order_acquire) != setupsDelivered_ ||
      resetsQueued_.load(std::memory_order_acquire) != resetsDelivered_) {
      pendingAddress_.store(kNoAddress, std::memory_order_relaxed);
   }
}

bool Stm32UsbDeviceController::openEndpoint(const art::usb::EndpointConfig &config) {
   const uint8_t number = endpointNumber(config.address);
   if(number == 0u) {
      // hw::UsbDevice opens EP0 itself on every bus reset. Opening it again
      // would reset its data toggles, and a SETUP packet can already be in.
      if(config.type != art::usb::EndpointType::Control || isInEndpoint(config.address) ||
         config.maxPacketSize != hw::UsbDevice::Ep0MaxPacketSize) {
         return false;
      }
      openIn_ |= kEndpoint0Bit;
      openOut_ |= kEndpoint0Bit;
      return true;
   }
   if(!device_.openEndpoint(config.address, toHardware(config.type), config.maxPacketSize)) {
      return false;
   }
   (isInEndpoint(config.address) ? openIn_ : openOut_) |= static_cast<uint8_t>(1u << number);
   return true;
}

void Stm32UsbDeviceController::closeEndpoint(uint8_t endpointAddress) {
   if(!isOpen(endpointAddress)) {
      return;
   }
   const uint8_t number = endpointNumber(endpointAddress);
   if(number == 0u) {
      openIn_ &= static_cast<uint8_t>(~kEndpoint0Bit);
      openOut_ &= static_cast<uint8_t>(~kEndpoint0Bit);
      return;
   }
   (isInEndpoint(endpointAddress) ? openIn_ : openOut_) &= static_cast<uint8_t>(~(1u << number));
   // hw::UsbDevice changes STAT with a read and a toggle write. If the
   // hardware completes a transaction in between, the write leaves a wrong
   // status. The endpoint is NAK after that transaction, so the second write
   // is race-free and corrects it.
   device_.setNak(endpointAddress);
   device_.setNak(endpointAddress);
   if(((openIn_ | openOut_) & static_cast<uint8_t>(~kEndpoint0Bit)) == 0u) {
      device_.closeAllEndpoints();
   }
}

void Stm32UsbDeviceController::setEndpointStall(uint8_t endpointAddress, bool stall) {
   if(!isOpen(endpointAddress)) {
      return;
   }
   device_.setStall(endpointAddress, stall);
   // The second write corrects the race of a transaction that completes
   // during the first one (see closeEndpoint()). Not on EP0: there a new
   // SETUP packet also ends the STALL, and a second write would stall the new
   // request. EP0 OUT also stays VALID after a clear, for the next SETUP.
   if(endpointNumber(endpointAddress) == 0u) {
      return;
   }
   if(!stall) {
      device_.setNak(endpointAddress);
   } else if(!device_.isStalled(endpointAddress)) {
      device_.setStall(endpointAddress, true);
   }
}

bool Stm32UsbDeviceController::startInTransfer(uint8_t endpointAddress, art::span<const uint8_t> packet) {
   if(!isInEndpoint(endpointAddress) || !isOpen(endpointAddress)) {
      return false;
   }
   return device_.startIn(endpointAddress, packet);
}

void Stm32UsbDeviceController::armOutTransfer(uint8_t endpointAddress) {
   if(isInEndpoint(endpointAddress) || !isOpen(endpointAddress)) {
      return;
   }
   static_cast<void>(device_.armOut(endpointAddress));
}

bool Stm32UsbDeviceController::isOpen(uint8_t endpointAddress) const {
   const uint8_t number = endpointNumber(endpointAddress);
   if(number >= hw::UsbDevice::EndpointCount) {
      return false;
   }
   const uint8_t open = isInEndpoint(endpointAddress) ? openIn_ : openOut_;
   return (open & (1u << number)) != 0u;
}

#endif
