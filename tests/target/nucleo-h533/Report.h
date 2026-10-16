#pragma once

// Test report on the ST-LINK virtual COM port (USART2, PA2/PA3, 115200 8N1).
// Each check prints one line, "PASS <name>" or "FAIL <name>". hwtest.py
// parses these lines, so keep the format.

#include <Serial.h>

namespace hwtest {

// The libart-stm32 Serial on USART2, interrupt-driven. All report output
// goes through it, so it also tests the TX interrupt path of Serial.
extern Serial vcp;

void startVcp();

/**
 * @brief Prints "PASS <name>" or "FAIL <name>" and counts the result.
 * @param hint Printed after a FAIL, e.g. the jumper wire that the check needs.
 * @return ok
 */
bool check(const char *name, bool ok, const char *hint = nullptr);

/// Prints "SUMMARY <passed> passed, <failed> failed".
void printSummary();

} // namespace hwtest
