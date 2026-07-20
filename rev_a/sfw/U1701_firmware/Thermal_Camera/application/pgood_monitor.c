
#include <stdio.h>

#include "application/pgood_monitor.h"

#include "gpio/pin_macros.h"

#include "usb_uart/terminal_control.h"

// this function prints current PGOOD status
void printPGOODStatus(void) {
 
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("Current Power Supply Control Status:\r\n");
   
    if (POS3P3_USB_PGOOD_PIN) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    +3.3V USB Voltage is %s\n\r", POS3P3_USB_PGOOD_PIN ? "within tolerance" : "out of tolerance");

    if (POS2P8_PGOOD_PIN) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    +2.8V Voltage is %s\n\r", POS2P8_PGOOD_PIN ? "within tolerance" : "out of tolerance");

    if (POS12_PGOOD_PIN) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    +12V Voltage is %s\n\r", POS12_PGOOD_PIN ? "within tolerance" : "out of tolerance");

    if (POS3P0_PGOOD_PIN) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    +3.0V Voltage is %s\n\r", POS3P0_PGOOD_PIN ? "within tolerance" : "out of tolerance");

    if (POS1P2_PGOOD_PIN) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    +1.2V Voltage is %s\n\r", POS1P2_PGOOD_PIN ? "within tolerance" : "out of tolerance");

    if (POS1P8_PGOOD_PIN) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    +1.8V Voltage is %s\n\r", POS1P8_PGOOD_PIN ? "within tolerance" : "out of tolerance");

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

    terminalTextAttributesReset();
}