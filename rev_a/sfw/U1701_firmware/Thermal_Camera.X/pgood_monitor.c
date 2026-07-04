
#include <stdio.h>

#include "pgood_monitor.h"

#include "pin_macros.h"

#include "terminal_control.h"

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

    terminalTextAttributesReset();
}