/* 
 * File:   main.c
 * Author: drewm
 *
 */

#include <xc.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

// #include "main.h"

// Core Drivers
#include "pic32mzda_configuration.h"
//#include "device_control.h"
//#include "32mz_interrupt_control.h"
//#include "heartbeat_timer.h"
//#include "watchdog_timer.h"
//#include "error_handler.h"
//#include "prefetch.h"
//#include "cause_of_reset.h"
//#include "rtcc.h"
//

// GPIO
#include "pin_macros.h"
#include "pic32mzda_gpio_setup.h"


//
//// Application
//#include "heartbeat_services.h"
//#include "power_saving.h"
//#include "telemetry.h"
//#include "carrier_spd.h"
//#include "pgood_monitor.h"


////// I2C
//#include "plib_i2c.h"
//#include "plib_i2c_master.h"
//#include "temperature_sensors.h"
//#include "power_monitors.h"
//#include "misc_i2c_devices.h"
//
////// USB
//#include "terminal_control.h"
//#include "uthash.h"
//#include "usb_uart.h"
//#include "usb_uart_rx_lookup_table.h"
//
////// ADC
//#include "adc.h"
//#include "adc_channels.h"


void main(void) {

    gpioInitialize();

    RESET_LED_PIN = LOW;
    
    while(true) {
        
        Nop();
        
    }

}

