

#include "telemetry.h"

#include <xc.h>
#include <stdio.h>

#include "terminal_control.h"
#include "pin_macros.h"
#include "pgood_monitor.h"

// This prints all telemetry data in an easily digested format
void printCurrentTelemetry(void) {
 
    // print off other random data points
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("\tMiscellaneous Telemetry:\033[K\r\n");
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("\t\tHost Die Temperature: %.3fC\033[K\r\n", telemetry.mcu_die_temp);
    printf("\t\tHost ADC Reference Voltage: %.3fV\033[K\r\n", telemetry.adc_vref_voltage);
    printf("\t\tHost Battery Voltage: %.3fV\033[K\r\n", telemetry.mcu_battery_voltage);
    printf("\t\tAmbient Temperature: %.3fC\033[K\r\n", telemetry.ambient_temperature);
    
    // print out state of PGOOD pins
    printPGOODStatus();
    
    printf("\r\n");
    
    terminalTextAttributesReset();
    
}