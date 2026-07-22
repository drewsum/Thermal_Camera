
#include <stdio.h>

#include "application/battery_monitor.h"

#include "gpio/pin_macros.h"

#include "usb_uart/terminal_control.h"

#include "i2c/i2c_devices.h"

#include "application/telemetry.h"

// this function prints the shared battery control/status GPIO signals
void printBatteryControlPins(void) {

    // The MAX8903's status outputs are all active low, so "ok" is the pin
    // reading low for everything down to BATT_IUSB_PIN
    terminalRow(TERMINAL_SGR_OK_BAD(!nBATT_FLT_PIN), "    Battery Manager is %s",
                nBATT_FLT_PIN ? "not faulted" : "faulted");

    terminalRow(TERMINAL_SGR_OK_BAD(!nBATT_DOK_PIN), "    Battery Manager DC Input is %s",
                nBATT_DOK_PIN ? "not stable" : "stable");

    terminalRow(TERMINAL_SGR_OK_BAD(!nBATT_UOK_PIN), "    Battery Manager USB Input is %s",
                nBATT_UOK_PIN ? "not stable" : "stable");

    terminalRow(TERMINAL_SGR_OK_BAD(!nBATT_CHG_PIN), "    Battery Manager is %s battery",
                nBATT_CHG_PIN ? "not charging" : "charging");

    terminalRow(TERMINAL_SGR_OK_BAD(!nBATT_CEN_PIN), "    Battery Manager charging is %s",
                nBATT_CEN_PIN ? "not enabled" : "enabled");

    terminalRow(TERMINAL_SGR_OK_BAD(BATT_IUSB_PIN), "    Battery Manager USB Current is %s",
                BATT_IUSB_PIN ? "500mA" : "100mA");

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
    terminalRow(TERMINAL_SGR_OK_BAD(BATT_LOWBATT_PIN),
                "    Fuel Gauge Low-Battery Indicator (GPOUT) reports %s",
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
