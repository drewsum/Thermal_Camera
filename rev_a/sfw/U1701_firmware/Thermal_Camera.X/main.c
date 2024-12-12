/* 
 * File:   main.c
 * Author: drewm
 *
 */

#include <xc.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

// Core Drivers
#include "pic32mzda_configuration.h"
#include "device_control.h"
#include "32mzda_interrupt_control.h"
#include "heartbeat_timer.h"
#include "watchdog_timer.h"
#include "prefetch.h"
#include "cause_of_reset.h"
//#include "rtcc.h"

// GPIO
#include "pin_macros.h"
#include "pic32mzda_gpio_setup.h"


//
//// Application
//#include "heartbeat_services.h"
#include "main.h"
#include "power_saving.h"
//#include "telemetry.h"
//#include "pgood_monitor.h"


////// I2C
//#include "plib_i2c.h"
//#include "plib_i2c_master.h"
//#include "temperature_sensors.h"
//#include "power_monitors.h"
//#include "misc_i2c_devices.h"
//
//// USB
#include "terminal_control.h"
#include "uthash.h"
#include "usb_uart.h"
#include "usb_uart_rx_lookup_table.h"
//
////// ADC
//#include "adc.h"
//#include "adc_channels.h"


void main(void) {

    
    // Save the cause of the most recent device reset
    // This also checks for configuration errors
    reset_cause = getResetCause();
    
    // Clear the terminal
    terminalClearScreen();
    terminalSetCursorHome();
    
    // set serial terminal window name
//    char *terminal_title_str;
//    terminal_title_str = (char *) malloc(64);
//    sprintf(terminal_title_str, "%s Serial Terminal", PROJECT_NAME_STR);
//    terminalSetTitle(terminal_title_str);
//    free(terminal_title_str);
    
    terminalTextAttributesReset();
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("%s\r\n", PROJECT_NAME_STR);
    printf("Host Firmware Version: %s, Platform Hardware Revision: %s\r\n", FIRMWARE_VERSION_STR, PLATFORM_REVISION_STR);
    printf("Created by Drew Maatman, %s\r\n", PROJECT_DATE_STR);
    
    printf("\r\n");
    
    printf(""
    "\r\n"        
    "  _____ _                               _    ____                               \r\n"
    " |_   _| |__   ___ _ __ _ __ ___   __ _| |  / ___|__ _ _ __ ___   ___ _ __ __ _ \r\n"
    "   | | | '_ \\ / _ \\ '__| '_ ` _ \\ / _` | | | |   / _` | '_ ` _ \\ / _ \\ '__/ _` |\r\n"
    "   | | | | | |  __/ |  | | | | | | (_| | | | |__| (_| | | | | | |  __/ | | (_| |\r\n"
    "   |_| |_| |_|\\___|_|  |_| |_| |_|\\__,_|_|  \\____\\__,_|_| |_| |_|\\___|_|  \\__,_|\r\n"
    "               \r\n");

    terminalTextAttributesReset();
    
     // Print cause of reset
    if (    reset_cause == Undefined ||
            reset_cause == Primary_Config_Registers_Error ||
            reset_cause == Primary_Secondary_Config_Registers_Error ||
            reset_cause == Config_Mismatch ||
            reset_cause == DMT_Reset ||
            reset_cause == WDT_Reset ||
            reset_cause == Software_Reset ||
            reset_cause == External_Reset ||
            reset_cause == BOR_Reset) {
    
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        
    }
    
    else {
     
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        
    }
    
//    // only clear persistent error flags if we've seen a POR... keep old values after other resets
//    if (reset_cause == POR_Reset) {
//        clearErrorHandler();
//    }
//
//    live_telemetry_enable = 0;
//    live_telemetry_print_request = 0;
//    
    printf("\r\nCause of most recent device reset: %s\r\n\r\n", getResetCauseString(reset_cause));
    terminalTextAttributesReset();
    
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("Beginning Host Initialization:\r\n");
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    
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
        
    // Enable Global Interrupts
    interruptControllerInitialize();
    enableGlobalInterrupts();
    printf("    Interrupt Controller Initialized, Global Interrupts Enabled\n\r");
    
    
    // Setup heartbeat timer
    heartbeatTimerInitialize();
    printf("    Heartbeat Timer Initialized\n\r");
        
    // Setup USB UART debugging
    usbUartInitialize();
    printf("    USB UART Initialized, DMA buffer method used, USB UART command hash table configured\n\r");
    
    // Setup prefetch module
    prefetchInitialize();
    printf("    CPU Instruction Prefetch Module Enabled\r\n");
    while(usbUartCheckIfBusy());
    
    // Disable unused peripherals for power savings
    PMDInitialize();
    printf("    Unused Peripheral Modules Disabled\n\r");
    while(usbUartCheckIfBusy());
    
    // setup watchdog timer
    watchdogTimerInitialize();
    printf("    Watchdog Timer Initialized\n\r");
    while(usbUartCheckIfBusy());
    
    // Disable reset LED
    RESET_LED_PIN = LOW;
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Reset LED Disabled, boot complete\r\n");
    while(usbUartCheckIfBusy());
    
    // Print end of boot message, reset terminal for user input
    terminalTextAttributesReset();
    terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("\n\rType 'Help' for list of supported commands\n\r\n\r");
    terminalTextAttributesReset();
    
    while(true) {
        
        // clear the watchdog if we need to
        if (wdt_clear_request) {
            kickTheDog();
            wdt_clear_request = 0;
        }
        
        // parse received USB strings if we have a new one received
        if (usb_uart_rx_ready) {
            usbUartRxLUTInterface(usb_uart_rx_buffer);
            // Determine length of received string
            uint32_t length = strlen(usb_uart_rx_buffer);
        
            // clear rx buffer
            uint32_t index;
            for (index = 0; index < length; index++) {
                usb_uart_rx_buffer[index] = '\0';
            }
        }
        
    }

}

