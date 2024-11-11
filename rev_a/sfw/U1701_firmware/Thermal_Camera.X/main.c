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
#include "device_control.h"
#include "32mzda_interrupt_control.h"
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

     // setup GPIO pins
    gpioInitialize();
    printf("    GPIO Pins Initialized\n\r");

    // block on POS3P0 and POS1P8 power stability
    while(POS3P0_PGOOD_PIN == LOW);
    while(POS1P8_PGOOD_PIN == LOW);
    printf("    Input power is stable\r\n");
    
    // Disable global interrupts so clocks can be initialized properly
    disableGlobalInterrupts();
    
    // Initialize system clocks
    clockInitialize();
    printf("    Oscillators, Phase-Locked Loop, and System Clocks Initialized\n\r");
    
    // Configure interrupt controller
    interruptControllerInitialize();
    
    // Enable Global Interrupts
    enableGlobalInterrupts();
    printf("    Interrupt Controller Initialized, Global Interrupts Enabled\n\r");
    
    RESET_LED_PIN = LOW;
    
    while(true) {
        
        Nop();
        
    }

}

