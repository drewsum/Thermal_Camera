
#include <stdio.h>

#include "application/battery_monitor.h"

#include "gpio/pin_macros.h"

#include "usb_uart/terminal_control.h"

#include "i2c/i2c_devices.h"

#include "application/telemetry.h"

// this function prints the shared battery control/status GPIO signals
void printBatteryControlPins(void) {

    if (nBATT_FLT_PIN) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Battery Manager is %s\n\r", nBATT_FLT_PIN ? "not faulted" : "faulted");

    if (nBATT_DOK_PIN) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Battery Manager DC Input is %s\n\r", nBATT_DOK_PIN ? "not stable" : "stable");

    if (nBATT_UOK_PIN) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Battery Manager USB Input is %s\n\r", nBATT_UOK_PIN ? "not stable" : "stable");

    if (nBATT_CHG_PIN) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Battery Manager is %s battery\n\r", nBATT_CHG_PIN ? "not charging" : "charging");

    if (nBATT_CEN_PIN) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Battery Manager charging is %s\n\r", nBATT_CEN_PIN ? "not enabled" : "enabled");

    if (BATT_IUSB_PIN) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Battery Manager USB Current is %s\n\r", BATT_IUSB_PIN ? "500mA" : "100mA");

    // BATT_LOWBATT_PIN is NOT a MAX8903 signal despite living in this same
    // pin block -- tracing the schematic shows it's driven by the BQ27441
    // fuel gauge's GPOUT pin, configured (BQ27441_ConfigureOpConfig(), in
    // i2c/device_driver/bq27441.c) via OpConfig[BATLOWEN]=1 to mirror the
    // Flags().SOC1 low-charge threshold, with OpConfig[GPIOPOL]=0 so the
    // pin reads LOW when SOC1 is asserted (battery low). This is
    // independent from, and shown alongside, the fuel gauge's own SOCF
    // flag in printBatteryStatus() -- one is a hardware GPIO mirror of the
    // SOC1 threshold, the other is polled over I2C against the SOCF
    // threshold; they can disagree.
    if (BATT_LOWBATT_PIN) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Fuel Gauge Low-Battery Indicator (GPOUT) reports %s\n\r",
           BATT_LOWBATT_PIN ? "battery not low" : "battery LOW");
}

// this function prints the "Battery" Platform Status? section
void printBatteryStatus(void) {

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("Battery / Charging Status:\r\n");

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Battery installed (at boot): %s\n\r", telemetry.battery.present ? "yes" : "no");

    if (I2CDevices_IsPresent(I2C_DEV_BATT_1)) {

        // Reuse telemetry.battery.* (already decoded by telemetryTasks())
        // rather than re-reading the gauge synchronously here, so this
        // command doesn't block on I2C.
        printf("    Voltage: %.3f V   Current: %.3f A   Temperature: %.2f C\n\r",
               telemetry.battery.voltage, telemetry.battery.current, telemetry.battery.temperature);
        printf("    Charge Level: %.0f%% (SOH %.0f%%), Remaining %.1f / %.1f mAh\n\r",
               telemetry.battery.state_of_charge, telemetry.battery.state_of_health,
               telemetry.battery.remaining_capacity, telemetry.battery.full_charge_capacity);

        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    State: %s%s%s\n\r",
               telemetry.battery.discharging ? "Discharging " : "",
               telemetry.battery.fully_charged ? "Fully Charged " : "",
               (!telemetry.battery.discharging && !telemetry.battery.fully_charged) ? "Charging/Idle " : "");

        if (telemetry.battery.low_battery) {
            terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("    LOW BATTERY (fuel gauge SOCF threshold)\n\r");
        }

        if (telemetry.battery.over_temperature || telemetry.battery.under_temperature) {
            terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("    %s%s\n\r",
                   telemetry.battery.over_temperature ? "OVER TEMPERATURE " : "",
                   telemetry.battery.under_temperature ? "UNDER TEMPERATURE " : "");
        }

    } else {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    Fuel gauge not detected on I2C bus\n\r");
    }

    terminalTextAttributesReset();
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("Battery Charger Pins:\r\n");
    printBatteryControlPins();

    terminalTextAttributesReset();
}
