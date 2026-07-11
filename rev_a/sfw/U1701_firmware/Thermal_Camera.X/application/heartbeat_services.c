
#include "application/heartbeat_services.h"

#include "application/main.h"
#include "application/error_handler.h"
#include "application/telemetry.h"
#include "core/device_control.h"
#include "usb_uart/terminal_control.h"
#include "gpio/pin_macros.h"

#include <stdio.h>

// This function executes actions every call of the heartbeat timer, and is used as an easy interface to do some action every second
void heartbeatServices(void) {

    if (live_telemetry_enable) {

        // get new temperature telemetry data every 200ms
        if ((heartbeat_systick + 5) % 20 == 0) temp_sense_data_request = 1;

        // NOTE: power_monitor_data_request is not implemented yet (no power
        // monitor I2C_DEVICE_KIND/driver exists yet) -- re-enable once it lands
        // get new telemetry data every 200ms
        if ((heartbeat_systick + 10) % 20 == 0) power_monitor_data_request = 1;

        /* Trigger an ADC conversion scan */
        if ((heartbeat_systick + 15) % 20 == 0) ADCCON3bits.GSWTRG = 1;

        // print new telemetry to terminal every second
        if (heartbeat_systick % 100 == 0) live_telemetry_print_request = 1;

    }

    // Update error LEDs based on error handler status
    update_error_leds_flag = 1;
    
    // Increment on time counter
    if (heartbeat_systick % 100 == 0) device_on_time_counter++;
    
    
    
}