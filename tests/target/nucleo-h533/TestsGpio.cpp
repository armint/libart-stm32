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
   // After reset the UCPD can pull CC1 (PB13) and CC2 (PB14) down with Rd
   // (5.1 kOhm). The internal pull-up (about 40 kOhm) cannot pull a pin high
   // against it. The option bit FLASH_OPTSR2.USBPD_DIS (set at delivery)
   // turns the Rd off on CC1, so the levels before the disable are only
   // information.
   const Pin cc1(GPIOB, 13);
   const Pin cc2(GPIOB, 14);
   cc1.configure(Pin::Mode::Input, Pin::OutputType::PushPull, Pin::Pull::Up);
   cc2.configure(Pin::Mode::Input, Pin::OutputType::PushPull, Pin::Pull::Up);
   delayuS(10);
   const bool cc1Before = cc1.read();
   const bool cc2Before = cc2.read();
   hw::Pwr::disableUcpdDeadBattery();
   delayuS(10);
   const bool cc1After = cc1.read();
   const bool cc2After = cc2.read();
   (void)vcp.printf("  PB13/PB14 with pull-up: %d/%d before, %d/%d after the dead-battery disable, USBPD_DIS %d\r\n",
                    cc1Before, cc2Before, cc1After, cc2After, (FLASH->OPTSR2_CUR & FLASH_OPTSR2_USBPD_DIS) != 0);
   check("ucpd-dead-battery-disable", (PWR->UCPDR & PWR_UCPDR_UCPD_DBDIS) != 0 && cc1After && cc2After);
   cc1.configure(Pin::Mode::Analog);
   cc2.configure(Pin::Mode::Analog);

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
   check("fastpin-direction-moder", isInput && isOutput);
   check("fastpin-input-reads-pa10", readHigh && readLow, GpioWire);

   pa10.configure(Pin::Mode::Analog);
   Pa9::pin().configure(Pin::Mode::Analog);
}

} // namespace hwtest
