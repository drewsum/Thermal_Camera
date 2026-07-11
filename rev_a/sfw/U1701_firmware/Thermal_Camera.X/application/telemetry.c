

#include "application/telemetry.h"

#include <xc.h>
#include <stdio.h>

#include "usb_uart/terminal_control.h"
#include "gpio/pin_macros.h"
#include "application/pgood_monitor.h"
#include "i2c/i2c_devices.h"

// This prints all telemetry data in an easily digested format
void printCurrentTelemetry(void) {
 
     // Print stuff off for POS12
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("\t+12V Power Input:\033[K\r\n");
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("\t\tVout: %.3fV"
           "\tIout: %.3fA"
           "\tPout: %.3fW\033[K\r\n"
           "\t\tTemp: %.3fC\033[K\r\n\033[K\r\n",
            telemetry.pos12.voltage,
            telemetry.pos12.current,
            telemetry.pos12.power,
            telemetry.pos12.temperature);

    // Print stuff off for POS3P0
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("\t+3.0V Power Supply:\033[K\r\n");
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("\t\tVout: %.3fV"
           "\tIout: %.3fA"
           "\tPout: %.3fW\033[K\r\n"
           "\t\tTemp: %.3fC\033[K\r\n\033[K\r\n",
            telemetry.pos3p0.voltage,
            telemetry.pos3p0.current,
            telemetry.pos3p0.power,
            telemetry.pos3p0.temperature);

    // Print stuff off for POS1P8
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("\t+1.8V Power Supply:\033[K\r\n");
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("\t\tVout: %.3fV"
           "\tIout: %.3fA"
           "\tPout: %.3fW\033[K\r\n"
           "\t\tTemp: %.3fC\033[K\r\n\033[K\r\n",
            telemetry.pos1p8.voltage,
            telemetry.pos1p8.current,
            telemetry.pos1p8.power,
            telemetry.pos1p8.temperature);

    // Print stuff off for POS2P8
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("\t+2.8V Power Supply:\033[K\r\n");
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("\t\tVout: %.3fV"
           "\tIout: %.3fA"
           "\tPout: %.3fW\033[K\r\n"
           "\t\tTemp: %.3fC\033[K\r\n\033[K\r\n",
            telemetry.pos2p8.voltage,
            telemetry.pos2p8.current,
            telemetry.pos2p8.power,
            telemetry.pos2p8.temperature);

    // Print stuff off for POS1P2
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("\t+1.2V Power Supply:\033[K\r\n");
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("\t\tVout: %.3fV"
           "\tIout: %.3fA"
           "\tPout: %.3fW\033[K\r\n"
           "\t\tTemp: %.3fC\033[K\r\n\033[K\r\n",
            telemetry.pos1p2.voltage,
            telemetry.pos1p2.current,
            telemetry.pos1p2.power,
            telemetry.pos1p2.temperature);

    // Print stuff off for Backlight
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("\tBacklight Power Supply:\033[K\r\n");
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("\t\tVout: %.3fV"
           "\tIout: %.3fA"
           "\tPout: %.3fW\033[K\r\n"
           "\t\tTemp: %.3fC\033[K\r\n\033[K\r\n",
            telemetry.backlight.voltage,
            telemetry.backlight.current,
            telemetry.backlight.power,
            telemetry.backlight.temperature);

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

// I2C_DEV_TEMP_1..7 (i2c_devices.h) are wired to physical MCP9804s in this
// exact order (POS12, POS3P0, POS1P8, POS2P8, POS1P2, Backlight, Ambient) --
// keep this table in sync with I2C_DEVICE_LIST if that list is ever reordered.
void updateTemperatureTelemetry(void) {

    I2C_DEVICE_TEMP_READING readings[I2C_DEVICE_COUNT] = { 0 };

    I2CDevices_ReadAllTemperatures(readings);

    if (readings[I2C_DEV_TEMP_1].present) telemetry.pos12.temperature     = readings[I2C_DEV_TEMP_1].celsius;
    if (readings[I2C_DEV_TEMP_2].present) telemetry.pos3p0.temperature    = readings[I2C_DEV_TEMP_2].celsius;
    if (readings[I2C_DEV_TEMP_3].present) telemetry.pos1p8.temperature    = readings[I2C_DEV_TEMP_3].celsius;
    if (readings[I2C_DEV_TEMP_4].present) telemetry.pos2p8.temperature    = readings[I2C_DEV_TEMP_4].celsius;
    if (readings[I2C_DEV_TEMP_5].present) telemetry.pos1p2.temperature    = readings[I2C_DEV_TEMP_5].celsius;
    if (readings[I2C_DEV_TEMP_6].present) telemetry.backlight.temperature = readings[I2C_DEV_TEMP_6].celsius;
    if (readings[I2C_DEV_TEMP_7].present) telemetry.ambient_temperature   = readings[I2C_DEV_TEMP_7].celsius;

}

// Reads voltage/current/power for `id` and updates whichever of `dest`'s
// fields read successfully, leaving the rest untouched.
static void UpdatePowerMonitorField(I2C_DEVICE_ID id, volatile telemetry_parameters_ps_t *dest) {

    float voltage;
    float current;
    float power;

    if (I2CDevices_ReadVoltage(id, &voltage)) dest->voltage = voltage;
    if (I2CDevices_ReadCurrent(id, &current)) dest->current = current;
    if (I2CDevices_ReadPower(id, &power))     dest->power   = power;

}

// I2C_DEV_PWR_1..6 (i2c_devices.h) are wired to physical INA231As in the
// same rail order as the I2C_DEV_TEMP_1..6 temperature sensors (POS12,
// POS3P0, POS1P8, POS2P8, POS1P2, Backlight) -- keep this table in sync
// with I2C_DEVICE_LIST if that list is ever reordered.
void updatePowerMonitorTelemetry(void) {

    UpdatePowerMonitorField(I2C_DEV_PWR_1, &telemetry.pos12);
    UpdatePowerMonitorField(I2C_DEV_PWR_2, &telemetry.pos3p0);
    UpdatePowerMonitorField(I2C_DEV_PWR_3, &telemetry.pos1p8);
    UpdatePowerMonitorField(I2C_DEV_PWR_4, &telemetry.pos2p8);
    UpdatePowerMonitorField(I2C_DEV_PWR_5, &telemetry.pos1p2);
    UpdatePowerMonitorField(I2C_DEV_PWR_6, &telemetry.backlight);

}