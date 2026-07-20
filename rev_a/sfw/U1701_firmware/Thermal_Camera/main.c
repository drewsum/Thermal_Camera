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
#include "core/pic32mzda_configuration.h"
#include "core/device_control.h"
#include "core/32mzda_interrupt_control.h"
#include "core/heartbeat_timer.h"
#include "core/watchdog_timer.h"
#include "core/prefetch.h"
#include "core/cause_of_reset.h"
#include "core/rtcc.h"
#include "core/hlvd.h"
#include "core/ddr2.h"

// SPI
#include "spi/spi3.h"
#include "spi/device_driver/sst25vf080b.h"
#include "spi/device_driver/sst25vf080b_disk.h"
#include "spi/flash_fileio.h"

// SDHC / microSD
#include "sdhc/sdhc.h"
#include "sdhc/device_driver/sd_card.h"
#include "sdhc/sd_fileio.h"

// USB (mass storage device)
#include "usb/usb.h"
#include "usb/device_driver/usb_msd.h"

// GLCD
#include "glcd/glcd.h"

// GPIO
#include "gpio/pin_macros.h"
#include "gpio/pic32mzda_gpio_setup.h"


//
//// Application
#include "application/error_handler.h"
#include "application/main.h"
#include "application/power_saving.h"
#include "application/heartbeat_services.h"
#include "application/telemetry.h"
#include "application/pgood_monitor.h"
#include "application/pushbuttons.h"
#include "application/backlight_pwm.h"


////// I2C
#include "i2c/i2c_master.h"
#include "i2c/i2c_devices.h"
//// USB UART
#include "usb_uart/terminal_control.h"
#include "usb_uart/uthash.h"
#include "usb_uart/usb_uart.h"
#include "usb_uart/usb_uart_rx_lookup_table.h"
//
////// ADC
#include "adc/adc.h"
#include "application/adc_channels.h"


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

    // Setup Power/Shutter pushbutton change-notification interrupts
    reportInit("Pushbuttons", pushbuttonsInitialize(), NULL);

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

    // Initialize the SST25VF080B SPI NOR flash on SPI3
    reportInit("SPI Flash (SST25VF080B)", SST25VF080B_Initialize(),
            &error_handler.flags.spi_flash_init_error);
    while(usbUartCheckIfBusy());

    // Bring up the SDHC peripheral itself (clocks/interrupt/register
    // defaults only, no card interaction) -- must succeed regardless of
    // whether a card happens to be inserted
    reportInit("SDHC Controller", SDHC_Initialize(),
            &error_handler.flags.sdhc_init_error);
    while(usbUartCheckIfBusy());

    // Card detection + mount is intentionally NOT wrapped in reportInit()/
    // an error_handler flag: an absent microSD card is normal, expected
    // removable-media behavior, not a controller fault. Only the SDHC
    // Controller line above reflects an actual init failure.
    terminalTextAttributesReset();
    if (SD_Card_Initialize() && SDFileIO_Mount()) {
        // Label a blank volume "SD" so it has a name when a USB host
        // mounts the card (never overwrites an existing label)
        SDFileIO_EnsureLabel();
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    microSD card detected and mounted\r\n");
    } else {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    No microSD card detected\r\n");
    }
    terminalTextAttributesReset();
    while(usbUartCheckIfBusy());

    // FAT volume "1:" on the SPI flash (512B-sector disk layer over the
    // 4KB-erase part, then mount -- formats on first boot, so the
    // "formatting..." notice is expected exactly once per blank part)
    reportInit("SPI Flash Filesystem",
            Flash_Disk_Initialize() && FlashFileIO_MountAndFormatIfNeeded(),
            &error_handler.flags.flash_fs_init_error);
    while(usbUartCheckIfBusy());

    // USB mass storage device (native USBHS module to the on-board hub):
    // exposes the SD card (LUN 0) and SPI flash (LUN 1) as two removable
    // drives to a USB host. Requires PMDInitialize() above to have left
    // the USB module enabled.
    reportInit("USB Mass Storage Device", USB_Initialize(),
            &error_handler.flags.usb_msd_init_error);
    while(usbUartCheckIfBusy());

    // probe every device in I2C_DEVICE_LIST (7x MCP9804 temp sensors + 6x
    // INA231A power monitors); I2CDevices_Initialize() records each device's
    // own pass/fail into error_handler.flags.<I2C_DEVICE_ID>_i2c_error (the
    // same flag its runtime reads later latch into on a NACK/timeout), so no
    // single aggregate flag is passed here
    reportInit("I2C Devices", I2CDevices_Initialize(), NULL);
    while(usbUartCheckIfBusy());

    // Bring up the Graphics LCD Controller for the on-board
    // GLT035320240IS1-CTP panel: programs timing/Layer 0 from a blank
    // (zeroed) frame buffer in DDR2 and drives the panel reset sequence.
    // The frame buffer is intentionally left blank -- filling it with
    // actual image data is a separate step. Must come after ddr2Initialize()
    // above (the frame buffer lives in DDR2).
    reportInit("Graphics LCD Controller", GLCD_Initialize(),
            &error_handler.flags.glcd_init_error);
    while(usbUartCheckIfBusy());

    // Backlight brightness is PWM-driven (OC3/Timer4, application/backlight_pwm.c)
    // rather than a plain digital enable pin -- must be initialized after
    // clockInitialize() above (which sets CFGCON.OCACLK=1 under unlock,
    // establishing the OC3-to-Timer4 pairing this driver verifies at init;
    // see backlight_pwm.h for the full story).
    reportInit("Backlight PWM", BacklightPWM_Initialize(),
            &error_handler.flags.backlight_pwm_init_error);
    while(usbUartCheckIfBusy());

    #warning "CTP touch controller detection is disabled for now, so the LCD backlight will always be enabled. Re-enable it when the touch controller is working."
//    // Enable the LCD backlight only if the panel's integrated GT911
//    // capacitive touch controller responded during I2C bring-up above --
//    // its I2C ACK is a reliable proxy for "the LCD module is actually
//    // populated on this board" (the GLCD Controller itself has no way to
//    // detect a physically-attached panel; it only configures MCU-internal
//    // registers). Left off entirely if I2C_DEV_CTP_1 wasn't found.
//    if (I2CDevices_IsPresent(I2C_DEV_CTP_1)) {
//        BacklightPWM_SetBrightness(100);
//        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
//        printf("    LCD Backlight Enabled (touch controller present)\r\n");
//    } else {
//        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
//        printf("    LCD Backlight left OFF (touch controller not detected)\r\n");
//    }
//    terminalTextAttributesReset();
//    while(usbUartCheckIfBusy());

    BacklightPWM_SetBrightness(100);

    // Disable reset LED
    RESET_LED_PIN = LOW;
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Reset LED Disabled, boot complete\r\n");
    while(usbUartCheckIfBusy());
    
    // Total elapsed on-time and power-cycle count, from the DS1683 total-
    // elapsed-time and event recorder (I2C_DEV_ETR_1) -- pulled out here
    // ahead of the generic I2C dump below since these two numbers are the
    // ones an operator most often cares about at a glance. DS1683_PrintStatus()
    // (called from I2CDevices_PrintStatus() further down) still prints the
    // rest of the device's status (command/config registers, alarm flags).
    {
        uint32_t elapsedSeconds;
        uint16_t powerCycleCount;

        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
        printf("\r\nElapsed Time / Power Cycle Status:\r\n");
        terminalTextAttributesReset();

        if (I2CDevices_ReadElapsedSeconds(I2C_DEV_ETR_1, &elapsedSeconds)) {
            uint32_t days  = elapsedSeconds / 86400u;
            uint32_t hours = (elapsedSeconds / 3600u) % 24u;
            uint32_t mins  = (elapsedSeconds / 60u) % 60u;
            uint32_t secs  = elapsedSeconds % 60u;

            terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("    Total Board Time: %lu s (%lud %02lu:%02lu:%02lu)\r\n",
                   (unsigned long)elapsedSeconds, (unsigned long)days,
                   (unsigned long)hours, (unsigned long)mins, (unsigned long)secs);
        } else {
            terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("    Total Board Time: unavailable (I2C error: %d)\r\n", (int)I2C_ErrorGet());
        }

        if (I2CDevices_ReadEventCount(I2C_DEV_ETR_1, &powerCycleCount)) {
            terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("    Power Cycle Count:   %u\r\n", powerCycleCount);
        } else {
            terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("    Power Cycle Count:   unavailable (I2C error: %d)\r\n", (int)I2C_ErrorGet());
        }

        terminalTextAttributesReset();
    }

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

        // run the USB device stack if the ISR latched events (bus events,
        // EP0 control traffic, mass storage bulk transfers)
        if (usb_event_pending) USB_Tasks();

        // flush the SPI flash staging buffer after a write-idle period
        // (cheap compare when nothing is dirty)
        USB_MSD_TimedTasks();

        // mount/unmount on SD card insertion/removal edges (cheap flag
        // check; the Port A change-notice ISR latches the edge event)
        SDFileIO_HotSwapTasks();

        // queue I2C temperature sensor reads if heartbeatServices() requested it
        // (non-blocking: the I2C interrupt clocks the transfers out in the background)
        if (temp_sense_data_request) {
            updateTemperatureTelemetry();
            temp_sense_data_request = 0;
        }

        // queue I2C power monitor reads if heartbeatServices() requested it
        if (power_monitor_data_request) {
            updatePowerMonitorTelemetry();
            power_monitor_data_request = 0;
        }

        // time out wedged I2C transfers and restart the queue after a bus error
        I2C_Tasks();

        // fold any finished I2C telemetry reads into the telemetry struct
        telemetryTasks();

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

