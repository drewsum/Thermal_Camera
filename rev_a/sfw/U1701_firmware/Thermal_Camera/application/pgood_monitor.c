
#include <stdio.h>

#include "application/pgood_monitor.h"

#include "gpio/pin_macros.h"

#include "usb_uart/terminal_control.h"

#include "application/battery_monitor.h"

// this function prints current PGOOD status
void printPGOODStatus(void) {

    terminalRow(TERMINAL_SGR(GREEN_COLOR, BOLD_FONT), "Current Power Supply Control Status:");

    terminalRow(TERMINAL_SGR_OK_BAD(POS3P3_USB_PGOOD_PIN), "    +3.3V USB Voltage is %s",
                POS3P3_USB_PGOOD_PIN ? "within tolerance" : "out of tolerance");

    terminalRow(TERMINAL_SGR_OK_BAD(POS2P8_PGOOD_PIN), "    +2.8V Voltage is %s",
                POS2P8_PGOOD_PIN ? "within tolerance" : "out of tolerance");

    terminalRow(TERMINAL_SGR_OK_BAD(POS12_PGOOD_PIN), "    +12V Voltage is %s",
                POS12_PGOOD_PIN ? "within tolerance" : "out of tolerance");

    terminalRow(TERMINAL_SGR_OK_BAD(POS3P0_PGOOD_PIN), "    +3.0V Voltage is %s",
                POS3P0_PGOOD_PIN ? "within tolerance" : "out of tolerance");

    terminalRow(TERMINAL_SGR_OK_BAD(POS1P2_PGOOD_PIN), "    +1.2V Voltage is %s",
                POS1P2_PGOOD_PIN ? "within tolerance" : "out of tolerance");

    terminalRow(TERMINAL_SGR_OK_BAD(POS1P8_PGOOD_PIN), "    +1.8V Voltage is %s",
                POS1P8_PGOOD_PIN ? "within tolerance" : "out of tolerance");

    printBatteryControlPins();

    terminalTextAttributesReset();
}