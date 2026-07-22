
#include "application/heartbeat_services.h"

#include "application/main.h"
#include "application/error_handler.h"
#include "application/telemetry.h"
#include "core/device_control.h"
#include "gui/gui.h"
#include "usb_uart/terminal_control.h"
#include "gpio/pin_macros.h"

#include <stdio.h>

// This function executes actions every call of the heartbeat timer, and is used as an easy interface to do some action every second
void heartbeatServices(void) {

    // get new battery fuel gauge data every 200ms
    if ((heartbeat_systick + 20) % 20 == 0) battery_data_request = 1;

    if (live_telemetry_enable) {

        // get new temperature telemetry data every 200ms
        if ((heartbeat_systick + 5) % 20 == 0) temp_sense_data_request = 1;

        // get new power monitor telemetry data every 200ms
        if ((heartbeat_systick + 10) % 20 == 0) power_monitor_data_request = 1;

        /* Trigger an ADC conversion scan */
        if ((heartbeat_systick + 15) % 20 == 0) ADCCON3bits.GSWTRG = 1;

        // print new telemetry to terminal every second
        if (heartbeat_systick % 100 == 0) live_telemetry_print_request = 1;

        // A refresh only rewrites the rows whose text changed, so anything
        // else that prints to the terminal (an error message, a command
        // response) leaves the page corrupted with no way to notice. Repaint
        // it in full every 30s so it heals itself, at 1/30th the link traffic
        // of the old repaint-every-second behaviour.
        if (heartbeat_systick % 3000 == 0) live_telemetry_full_repaint = 1;

    }

    // Ask GUI_Tasks() to re-read the values shown on the GUI overlay (clock,
    // ambient temperature, battery) every 500ms. Outside the
    // live_telemetry_enable block above on purpose: the panel keeps updating
    // whether or not the terminal is in live telemetry mode.
    if (heartbeat_systick % 50 == 0) gui_refresh_request = 1;

    // Update error LEDs based on error handler status
    update_error_leds_flag = 1;
    
    // Increment on time counter
    if (heartbeat_systick % 100 == 0) device_on_time_counter++;
    
    
    
}