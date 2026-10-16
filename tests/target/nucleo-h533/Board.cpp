#include "Board.h"

namespace hwtest {

void alternate(GPIO_TypeDef *port, uint8_t pin, Pin::AlternateFunction af, Pin::Pull pull) {
   const Pin p(port, pin);
   p.setAlternateFunction(af);
   p.configure(Pin::Mode::Alternate, Pin::OutputType::PushPull, pull, Pin::Speed::VeryHigh);
}

void startCycleCounter() {
   CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
   DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

bool within(uint64_t value, uint64_t expected, uint32_t ppm) {
   const uint64_t difference = value > expected ? value - expected : expected - value;
   return difference * 1000000U <= expected * ppm;
}

} // namespace hwtest
