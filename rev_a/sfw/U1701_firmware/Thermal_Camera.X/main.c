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
#include "rtcc.h"
#include "hlvd.h"
#include "ddr2.h"

// GPIO
#include "pin_macros.h"
#include "pic32mzda_gpio_setup.h"


//
//// Application
#include "error_handler.h"
#include "main.h"
#include "power_saving.h"
#include "heartbeat_services.h"
#include "telemetry.h"
#include "pgood_monitor.h"


////// I2C
#include "plib_i2c.h"
#include "i2c_devices.h"
//// USB
#include "terminal_control.h"
#include "uthash.h"
#include "usb_uart.h"
#include "usb_uart_rx_lookup_table.h"
//
////// ADC
#include "adc.h"
#include "adc_channels.h"


// Prints a boot initialization result line and records failures. On success it
// prints "    <label> Initialized" in green; on failure it prints
// "    <label> FAILED to initialize" in bold red and sets *error_flag (pass
// NULL for subsystems with no dedicated init flag). Returns ok unchanged so it
// can wrap an init call inline. Leaves the terminal in green/normal afterward.
static bool reportInit(const char *label, bool ok, volatile uint8_t *error_flag) {

    if (ok) {

        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    %s Initialized\r\n", label);

    }

    else {

        if (error_flag != NULL) *error_flag = 1;
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, BOLD_FONT);
        printf("    %s FAILED to initialize\r\n", label);
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);

    }

    return ok;

}

void main(void) {

    
    // Save the cause of the most recent device reset
    // This also checks for configuration errors
    reset_cause = getResetCause();
    
    // Clear the terminal
    terminalClearScreen();
    terminalSetCursorHome();
    
    // set serial terminal window name
    char *terminal_title_str;
    terminal_title_str = (char *) malloc(64);
    sprintf(terminal_title_str, "%s Serial Terminal", PROJECT_NAME_STR);
    terminalSetTitle(terminal_title_str);
    free(terminal_title_str);
    
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
            reset_cause == BOR_Reset ||
            reset_cause == VBAT_POR) {

        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);

    }

    else {

        // Deep_Sleep_Reset and VBAT_Wake are expected outcomes of the
        // low-power features this device is configured for, not faults
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);

    }

    // Deep Sleep exit, VBAT wake, and VBAT POR all re-arm a POR at the
    // hardware level (RAM/SFRs reset the same as a plain POR), so persistent
    // error flags are just as unreliable after these as after a plain POR
    if (    reset_cause == POR_Reset ||
            reset_cause == Deep_Sleep_Reset ||
            reset_cause == VBAT_Wake ||
            reset_cause == VBAT_POR) {
        clearErrorHandler();
        live_telemetry_enable = 0;
    }

    errorHandlerInitialize();

    // live_telemetry_print_request is not persistent, so always clear it at boot
    live_telemetry_print_request = 0;

    printf("\r\nCause of most recent device reset: %s\r\n\r\n", getResetCauseString(reset_cause));
    terminalTextAttributesReset();
    
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("Beginning Host Initialization:\r\n");
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    
     // setup GPIO pins
    reportInit("GPIO Pins", gpioInitialize(), NULL);

    // block on POS3P0 and POS1P8 power stability
    while(POS3P0_PGOOD_PIN == LOW);
    while(POS1P8_PGOOD_PIN == LOW);
    printf("    Input power is stable\r\n");
    
    // Disable global interrupts so clocks can be initialized properly
    disableGlobalInterrupts();
    
    // Initialize system clocks
    reportInit("Oscillators, PLL, and System Clocks", clockInitialize(),
            &error_handler.flags.clock_init_error);
        
    // Enable Global Interrupts
    bool interrupts_ok = interruptControllerInitialize();
    enableGlobalInterrupts();
    reportInit("Interrupt Controller", interrupts_ok, NULL);
    
    // Setup error handling
    reportInit("Error Handler", errorHandlerInitialize(), NULL);
    
    // Setup heartbeat timer
    reportInit("Heartbeat Timer", heartbeatTimerInitialize(),
            &error_handler.flags.heartbeat_timer_init_error);
        
    // Setup USB UART debugging
    reportInit("USB UART", usbUartInitialize(),
            &error_handler.flags.usb_uart_init_error);
    
    // Setup prefetch module
    reportInit("CPU Instruction Prefetch Module", prefetchInitialize(),
            &error_handler.flags.prefetch_init_error);
    while(usbUartCheckIfBusy());
    
    // Disable unused peripherals for power savings
    reportInit("Peripheral Module Disable (PMD)", PMDInitialize(),
            &error_handler.flags.pmd_init_error);
    while(usbUartCheckIfBusy());

    // setup watchdog timer
    reportInit("Watchdog Timer", watchdogTimerInitialize(),
            &error_handler.flags.watchdog_init_error);
    while(usbUartCheckIfBusy());
    
    bool rtcc_ok = rtccInitialize();
    // Deep_Sleep_Reset and VBAT_Wake keep the RTCC running across the event
    // specifically so its time doesn't need to be cleared here -- only clear
    // it when the time was never reliably set (POR) or the backup battery
    // that was supposed to maintain it is missing/depleted (VBAT_POR)
    if (reset_cause == POR_Reset || reset_cause == VBAT_POR) rtccClear();
    reportInit("Real Time Clock-Calendar", rtcc_ok,
            &error_handler.flags.rtcc_init_error);
    while(usbUartCheckIfBusy());
    
    // Enable ADC
    reportInit("Analog to Digital Converter", ADCInitialize(),
            &error_handler.flags.adc_init_error);
    while(usbUartCheckIfBusy());
    
    // setup I2C
    reportInit("I2C Bus Master", I2C_Initialize(),
            &error_handler.flags.i2c_init_error);
    while(usbUartCheckIfBusy());
    
    // setup HLVD
    bool hlvd_ok = hlvdInitialize(5, HLVD_DIRECTION_LOW_VOLTAGE);
    while(!hlvdIsReady());
    reportInit("HLVD", hlvd_ok && hlvdIsReady(),
            &error_handler.flags.hlvd_init_error);
    while(usbUartCheckIfBusy());
    
    // Initialize the 32MB DDR2 SDRAM stacked in this device's package
    reportInit("DDR2 SDRAM Controller", ddr2Initialize(),
            &error_handler.flags.ddr2_init_error);
    while(usbUartCheckIfBusy());

    // probe every device in I2C_DEVICE_LIST (currently 7x MCP9804 temp sensors)
    reportInit("I2C Devices", I2CDevices_Initialize(),
            &error_handler.flags.i2c_devices_init_error);
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

            // clear rx buffer
            memset(usb_uart_rx_buffer, 0, strlen(usb_uart_rx_buffer));
        }
    
        
        if (live_telemetry_print_request && live_telemetry_enable) {
            
            // Clear the terminal
            terminalClearScreen();
            terminalSetCursorHome();
            
            terminalTextAttributesReset();
            terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, BOLD_FONT);
            printf("Live system telemetry:\033[K\n\r\033[K");
            
            printCurrentTelemetry();
            
            terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("Call 'Live Telemetry' command to disable\033[K\n\r");
            terminalTextAttributesReset();
            
            live_telemetry_print_request = 0;
            
        }
        
        // check to see if a clock fail has occurred and latch it
        clockFailCheck();
        
        if (hlvdCheckAndClearLatchedEvent()) {
            error_handler.flags.mcu_vdd_hlvd_brownout = 1;
        }
        
        if (hlvdCoreCheckAndClearEvent()) {
            error_handler.flags.mcu_vdd_core_hlvd_brownout = 1;
        }
        
        // update error LEDs if needed
        if (update_error_leds_flag) updateErrorLEDs();
        
    }

}

