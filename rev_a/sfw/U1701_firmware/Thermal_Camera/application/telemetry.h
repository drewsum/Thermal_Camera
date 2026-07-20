/* ************************************************************************** */
/** Descriptive File Name

  @Company
    Company Name

  @File Name
    filename.h

  @Summary
    Brief description of the file.

  @Description
    Describe the purpose of this file.
 */
/* ************************************************************************** */

#ifndef _TELEMETRY_H    /* Guard against multiple inclusion */
#define _TELEMETRY_H

#include <xc.h>
#include <stdbool.h>

// This is a typedef for holding the different types of telemetry data for each power supply
typedef struct telemetry_paramaters_ps_u {

    double temperature;
    double voltage;
    double current;
    double power;

} telemetry_parameters_ps_t;

// This is a typedef for holding the BQ27441 fuel gauge's telemetry data
typedef struct telemetry_battery_s {

    double voltage;                 // V
    double current;                 // A, signed (+ = charging, - = discharging)
    double temperature;             // C, external NTC via BIN
    double state_of_charge;         // %, 0-100
    double state_of_health;         // %, 0-100
    double remaining_capacity;      // mAh
    double full_charge_capacity;    // mAh

    // Latched ONCE at boot in main.c from a Voltage()-threshold heuristic
    // (the fuel gauge's Flags().BAT_DET can't distinguish "battery
    // installed" from "not installed" on this board -- see main.c). Not
    // updated afterward by telemetryTasks(); ongoing I2C comm health for
    // this device is tracked separately via
    // error_handler.flags.I2C_DEV_BATT_1_i2c_error, not here.
    bool present;

    // Decoded from Flags() every telemetry cycle
    bool charging;             // Flags().CHG
    bool discharging;          // Flags().DSG
    bool fully_charged;        // Flags().FC
    bool over_temperature;     // Flags().OT
    bool under_temperature;    // Flags().UT
    bool low_battery;          // Flags().SOCF

} telemetry_battery_t;


// This is the structure that holds all telemetry data in the entire system
volatile __attribute__((coherent)) struct telemetry_s {
    
    telemetry_parameters_ps_t pos12;
    telemetry_parameters_ps_t pos3p0;
    telemetry_parameters_ps_t pos1p8;
    telemetry_parameters_ps_t pos2p8;
    telemetry_parameters_ps_t pos1p2;
    telemetry_parameters_ps_t backlight;
    telemetry_battery_t battery;
    double ambient_temperature;
    double mcu_die_temp;
    double mcu_battery_voltage;
    double adc_vref_voltage;

} telemetry;

// These flags are used to keep enable and request live telemetry updates
volatile __attribute__((coherent)) uint8_t live_telemetry_enable;
volatile __attribute__((coherent)) uint8_t live_telemetry_print_request;

// This prints all telemetry data in an easily digested format
void printCurrentTelemetry(void);

// Queues a non-blocking I2C read of every temperature sensor (via
// i2c_devices.h) and returns immediately -- the I2C interrupt runs the
// transfers in the background and telemetryTasks() folds the results into
// the temperature fields above. Call from main()'s loop, not from
// ISR/heartbeat context. A sensor that fails to respond leaves its previous
// telemetry value untouched rather than clobbering it with a bad reading.
void updateTemperatureTelemetry(void);

// Same as updateTemperatureTelemetry(), but queues the voltage/current/power
// reads of every I2C power monitor.
void updatePowerMonitorTelemetry(void);

// Same as updateTemperatureTelemetry(), but queues every standard-command
// read the BQ27441 fuel gauge's telemetry.battery fields need (voltage,
// current, temperature, state of charge/health, capacities, flags).
void updateBatteryTelemetry(void);

// Decodes any finished I2C telemetry reads (queued by the two functions
// above) into the telemetry struct. Call once per main-loop iteration.
void telemetryTasks(void);


#endif /* _TELEMETRY_H */

/* *****************************************************************************
 End of File
 */
