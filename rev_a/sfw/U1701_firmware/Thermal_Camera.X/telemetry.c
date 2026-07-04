

#include "telemetry.h"

#include <xc.h>
#include <stdio.h>

#include "terminal_control.h"
#include "pin_macros.h"
#include "pgood_monitor.h"

// This prints all telemetry data in an easily digested format
void printCurrentTelemetry(void) {
 
    // Print stuff off for POS24
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("\t+24V Power Input:\033[K\r\n");
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("\t\tVout: %.3fV"
           "\tIout: %.3fA"
           "\tPout: %.3fW\033[K\r\n"
           "\t\tTemp: %.3fC\033[K\r\n\033[K\r\n",
            telemetry.pos24.voltage,
            telemetry.pos24.current,
            telemetry.pos24.power,
            telemetry.pos24.temperature);    
    
    // Print stuff off for POS3P3
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("\t+3.3V Power Supply:\033[K\r\n");
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("\t\tVout: %.3fV"
           "\tIout: %.3fA"
           "\tPout: %.3fW\033[K\r\n"
           "\t\tTemp: %.3fC\033[K\r\n\033[K\r\n",
            telemetry.pos3p3.voltage,
            telemetry.pos3p3.current,
            telemetry.pos3p3.power,
            telemetry.pos3p3.temperature); 
    
    // Print stuff off for POS180
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("\t+180V Power Supply:\033[K\r\n");
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("\t\tVout: %.3fV"
            "\t+90V Termination Voltage: %.3fV"
           "\tIout: %.3fmA"
           "\tPout: %.3fW\033[K\r\n"
           "\t\tTemp: %.3fC\033[K\r\n\033[K\r\n",
            telemetry.pos180.voltage,
            telemetry.pos90_termination_voltage,
            telemetry.pos180.current * 1000.0,
            (telemetry.pos180.voltage * telemetry.pos180.current),
            telemetry.pos180.temperature); 
    
    // print off other random data points
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("\tMiscellaneous Telemetry:\033[K\r\n");
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("\t\tHost Die Temperature: %.3fC\033[K\r\n", telemetry.mcu_die_temp);
    printf("\t\tHost ADC Reference Voltage: %.3fV\033[K\r\n", telemetry.adc_vref_voltage);
    printf("\t\tAmbient Temperature: %.3fC\033[K\r\n", telemetry.ambient_temperature);
    printf("\t\tBackup RTC Die Temperature: %.3fC\033[K\r\n", telemetry.backup_rtc_temperature);
    printf("\t\tBackup Battery Voltage: %.3fV\033[K\r\n\r\n", telemetry.backup_battery_voltage);
    
    // print out state of PGOOD pins
    printPGOODStatus();
    
    printf("\r\n");
    
    terminalTextAttributesReset();

    
}