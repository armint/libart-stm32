#include "Board.h"

namespace hwtest {

void alternate(GPIO_TypeDef *port, uint8_t pin, Pin::AlternateFunction af, Pin::Pull pull) {
   const Pin p(port, pin);
   p.setAlternateFunction(af);
   p.configure(Pin::Mode::Alternate, Pin::OutputType::PushPull, pull, Pin::Speed::VeryHigh);
}

void uncacheReadOnlyArea() {
   ARM_MPU_Disable();
   ARM_MPU_SetMemAttr(0, ARM_MPU_ATTR(ARM_MPU_ATTR_NON_CACHEABLE, ARM_MPU_ATTR_NON_CACHEABLE));
   // Read-only, unprivileged access allowed, execute never.
   ARM_MPU_SetRegion(0, ARM_MPU_RBAR(0x08FFF000UL, ARM_MPU_SH_NON, 1U, 1U, 1U), ARM_MPU_RLAR(0x08FFFFFFUL, 0U));
   // PRIVDEFENA: the default memory map stays valid outside region 0.
   ARM_MPU_Enable(MPU_CTRL_PRIVDEFENA_Msk);
}

void startCycleCounter() {
   CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
   DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

bool within(uint64_t value, uint64_t expected, uint32_t ppm) {
   const uint64_t difference = value > expected ? value - expected : expected - value;
   return difference * 1000000U <= expected * ppm;
}

bool waitChannel(const Dma::Channel &channel) {
   return waitFor([&] { return channel.isTransferCompleteFlagSet() || channel.isErrorFlagSet(); }, 10000) &&
          !channel.isErrorFlagSet();
}

} // namespace hwtest
