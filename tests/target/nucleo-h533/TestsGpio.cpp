#include "Board.h"
#include "Report.h"
#include "Tests.h"

#include <delaybusywait.h>
#include <peripherals/FastPin.h>
// hw::Pwr::setStopMode() has no STM32H5 branch, so its parameter is unused
// there. Remove the pragmas when system-stm32 marks it [[maybe_unused]].
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <peripherals/Pwr.h>
#pragma GCC diagnostic pop

namespace hwtest {
namespace {

using Pa9 = hw::FastPin<GPIOA_BASE, 9>;
constexpr const char *GpioWire = "needs the wire PA9-PA10";

uint32_t pa9Mode() {
   return (GPIOA->MODER >> (9U * 2U)) & 3U;
}

} // namespace

void testGpio() {
   // After reset the UCPD pulls CC1 (PB13) down with Rd (5.1 kOhm). The
   // internal pull-up (about 40 kOhm) cannot pull the pin high against it.
   const Pin cc1(GPIOB, 13);
   cc1.configure(Pin::Mode::Input, Pin::OutputType::PushPull, Pin::Pull::Up);
   delayuS(10);
   const bool before = cc1.read();
   hw::Pwr::disableUcpdDeadBattery();
   delayuS(10);
   const bool after = cc1.read();
   (void)vcp.printf("  PB13 with pull-up: %d before, %d after the dead-battery disable\r\n", before, after);
   check("ucpd-dead-battery-disable", !before && after);
   cc1.configure(Pin::Mode::Analog);

   // PA9 drives PA10.
   const Pin pa10(GPIOA, 10);
   pa10.configure(Pin::Mode::Input, Pin::OutputType::PushPull, Pin::Pull::Down);
   Pa9::pin().configure(Pin::Mode::Output);
   Pa9::write(true);
   delayuS(5);
   const bool high = pa10.read();
   Pa9::write(false);
   delayuS(5);
   const bool low = !pa10.read();
   check("gpio-pa9-drives-pa10", high && low, GpioWire);

   // FastPin direction switch: PA9 becomes an input and PA10 drives it.
   Pa9::setInput();
   const bool isInput = pa9Mode() == 0U;
   pa10.configure(Pin::Mode::Output);
   pa10.write(true);
   delayuS(5);
   const bool readHigh = Pa9::read();
   pa10.write(false);
   delayuS(5);
   const bool readLow = !Pa9::read();
   Pa9::setOutput();
   const bool isOutput = pa9Mode() == 1U;
   check("fastpin-set-input-set-output", isInput && isOutput && readHigh && readLow, GpioWire);

   pa10.configure(Pin::Mode::Analog);
   Pa9::pin().configure(Pin::Mode::Analog);
}

} // namespace hwtest
