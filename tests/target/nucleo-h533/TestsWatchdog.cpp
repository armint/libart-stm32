#include "Board.h"
#include "Report.h"
#include "Tests.h"

#include <delaybusywait.h>
#include <peripherals/Dbg.h>
#include <peripherals/Iwdg.h>

namespace hwtest {
namespace {

// TAMP_BKP0R holds this value while the IWDG reset of the test is due. The
// backup registers keep their value through a system reset.
constexpr uint32_t WatchdogArmed = 0x1D06A5EDU;

void enableBackupRegisters() {
   PWR->DBPCR |= PWR_DBPCR_DBP;
   RCC->APB3ENR |= RCC_APB3ENR_RTCAPBEN;
   [[maybe_unused]] const uint32_t readBack = RCC->APB3ENR;
}

} // namespace

bool useHsiRecipe(bool buttonPressed) {
   enableBackupRegisters();
   if(TAMP->BKP0R == WatchdogArmed) {
      return TAMP->BKP1R != 0; // second start: keep the choice of the first
   }
   TAMP->BKP1R = buttonPressed ? 1U : 0U;
   return buttonPressed;
}

void testWatchdogReset() {
   const uint32_t resetFlags = RCC->RSR;
   RCC->RSR |= RCC_RSR_RMVF;
   enableBackupRegisters();
   hw::Dbg::freezeIwdgOnDebug();

   if(TAMP->BKP0R == WatchdogArmed) {
      // Second start: the reset came from the IWDG, or the IWDG test failed.
      TAMP->BKP0R = 0;
      check("iwdg-reset", (resetFlags & RCC_RSR_IWDGRSTF) != 0, "the reset after the arm was not an IWDG reset");
      check("dbg-freeze-iwdg", (DBGMCU->APB1FZR1 & DBGMCU_APB1FZR1_DBG_IWDG_STOP) != 0);
      return;
   }

   (void)vcp.printf("  iwdg: 40 ms watchdog, the board resets now\r\n");
   (void)vcp.flush();
   // Not a poll of the TC flag: the Serial ISR clears it, so the poll can
   // hang. The last frame takes 87 us at 115200 baud.
   delaymS(20);
   TAMP->BKP0R = WatchdogArmed;
   const hw::Iwdg iwdg(IWDG);
   iwdg.enable();
   iwdg.configure(hw::Iwdg::Prescaler::Div4, 320); // 32 kHz / 4 / 320: 40 ms
   iwdg.reload();
   delaymS(1000);
   // No reset in 25 watchdog periods. The test continues without the reset.
   TAMP->BKP0R = 0;
   check("iwdg-reset", false, "no reset in 1 s");
}

void startWatchdog() {
   const hw::Iwdg iwdg(IWDG);
   iwdg.enable();
   iwdg.configure(hw::Iwdg::Prescaler::Div1024, 125); // 32 kHz / 1024 / 125: 4 s
   iwdg.reload();
   check("iwdg-prescaler-div1024", IWDG->PR == static_cast<uint32_t>(hw::Iwdg::Prescaler::Div1024));
}

} // namespace hwtest
