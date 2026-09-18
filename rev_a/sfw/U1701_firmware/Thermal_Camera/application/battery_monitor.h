/* ************************************************************************** */
/** Battery Monitor

  @Summary
    Prints battery/charging status: the MAX8903G charger's discrete GPIO
    signals and the BQ27441 fuel gauge's telemetry.

  @Description
    printBatteryControlPins() is shared between printPGOODStatus()
    (application/pgood_monitor.c) and printBatteryStatus() (below) so the
    pin block isn't duplicated between the two status sections.
 */
/* ************************************************************************** */

#ifndef _BATTERY_MONITOR_H    /* Guard against multiple inclusion */
#define _BATTERY_MONITOR_H

#include <xc.h>

// Prints the shared battery control/status GPIO signals: the MAX8903G
// charger's discrete pins (FLT/DOK/UOK/CHG/CEN/IUSB) AND BATT_LOWBATT_PIN
// (which is actually the BQ27441 fuel gauge's GPOUT pin, configured via
// OpConfig[BATLOWEN/GPIOPOL] -- see BQ27441_ConfigureOpConfig() -- to
// mirror the SOC1 low-charge threshold, NOT a MAX8903 signal despite
// living in this same pin block). Does not call
// terminalTextAttributesReset() -- the caller resets once at the end.
void printBatteryControlPins(void);

// Prints the "Battery" Platform Status? section: fuel-gauge state,
// charging status, and charge level, followed by
// printBatteryControlPins(). Self-contained banner +
// terminalTextAttributesReset(), matching printPGOODStatus()'s style.
void printBatteryStatus(void);

#endif /* _BATTERY_MONITOR_H */

/* *****************************************************************************
 End of File
 */
