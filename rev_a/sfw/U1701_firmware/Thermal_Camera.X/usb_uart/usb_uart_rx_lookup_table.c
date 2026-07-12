
#include <xc.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "usb_uart/usb_uart_rx_lookup_table.h"
#include "usb_uart/usb_uart.h"
#include "usb_uart/uthash.h"

#include "application/main.h"
#include "core/rtcc.h"
#include "application/error_handler.h"
#include "application/heartbeat_services.h"
#include "core/cause_of_reset.h"
#include "usb_uart/terminal_control.h"
#include "core/device_control.h"
#include "core/watchdog_timer.h"
#include "usb_uart/usb_uart.h"
#include "core/prefetch.h"
#include "application/power_saving.h"
#include "gpio/pin_macros.h"
#include "application/pgood_monitor.h"
#include "application/telemetry.h"
#include "i2c/i2c_master.h"
#include "adc/adc.h"
#include "application/adc_channels.h"
#include "core/hlvd.h"
#include "core/ddr2.h"
#include "i2c/i2c_devices.h"
#include "spi/spi3.h"
#include "spi/device_driver/sst25vf080b.h"

USB_UART_COMMAND(helpCommandFunction, "Help", "Prints help message for all supported serial commands") {

    terminalTextAttributesReset();
    terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("Supported Commands:\n\r");
    
    terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    
    // iterate over usb_uart_commands hash table and print the name of all commands and their help messages
    usb_uart_command_t  *current_command, *temp;
    HASH_ITER(hh, usb_uart_commands, current_command, temp) {
            
        printf("    %s: %s\r\n", current_command->command_name, current_command->command_help_message);
        
    }
    
    terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("\r\nHelp messages and neutral responses appear in yellow\n\r");
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("System parameters and affirmative responses appear in green\n\r");
    terminalTextAttributes(CYAN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("Measurement responses appear in cyan\n\r");
    terminalTextAttributes(MAGENTA_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("Urgent/interrupt messages appear in magenta\n\r");
    terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("Errors and negative responses appear in red\n\r");
    terminalTextAttributesReset();
    printf("User input appears in white\n\r");
         
    
}

USB_UART_COMMAND(resetCommand, "Reset", "Executes an MCU software reset") {
 
    deviceReset();
    
}

USB_UART_COMMAND(clearCommand, "Clear Screen", "Clears the serial port terminal") {

    terminalClearScreen();
    terminalSetCursorHome();
    
}

USB_UART_COMMAND(idnCommand, "IDN?", "Prints identification string") {
    terminalTextAttributesReset();
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("%s by Drew Maatman, %s, FW version %s\r\n", 
            PROJECT_NAME_STR, 
            PROJECT_DATE_STR, 
            FIRMWARE_VERSION_STR);
    terminalTextAttributesReset();
}

USB_UART_COMMAND(repositoryCommand, "Repository?", "Prints project Git repo location") {
    terminalTextAttributesReset();
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("Project Git repository is hosted at: %s\r\n", PROJECT_REPOSITORY_STR);
    terminalTextAttributesReset();    
}

USB_UART_COMMAND(mcuStatusCommand, "MCU Status?",
        "\b\b <section>: Prints status of MCU host device. If no argument is passed, prints everything. Available sections:\r\n"
        "       IDs\r\n"
        "       WDT\r\n"
        "       DMT\r\n"
        "       Prefetch\r\n"
        "       Cause of Reset\r\n"
        "       Up Time") {

    // Snipe out received arguments
    char rx_section_name[32] = {0};
    sscanf(input_str, "MCU Status? %[^\t\n\r]", rx_section_name);

    // No argument means print every section below
    bool print_all = (rx_section_name[0] == '\0');

    bool want_ids            = print_all || (strcmp(rx_section_name, "IDs") == 0);
    bool want_wdt             = print_all || (strcmp(rx_section_name, "WDT") == 0);
    bool want_dmt             = print_all || (strcmp(rx_section_name, "DMT") == 0);
    bool want_prefetch        = print_all || (strcmp(rx_section_name, "Prefetch") == 0);
    bool want_cause_of_reset  = print_all || (strcmp(rx_section_name, "Cause of Reset") == 0);
    bool want_up_time         = print_all || (strcmp(rx_section_name, "Up Time") == 0);
    bool matched_any = want_ids || want_wdt || want_dmt || want_prefetch ||
                        want_cause_of_reset || want_up_time;

    terminalTextAttributesReset();

    if (want_ids) {

        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("MCU Firmware Version: %s\r\n", FIRMWARE_VERSION_STR);

        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
        printf("MCU Device IDs:\r\n");
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);

        // Print serial number
        printf("    PIC32MZ Serial Number retrieved from Flash: %s\n\r",
                    getStringSerialNumber());

        // Print device ID
        printf("    Device ID retrieved from Flash: %s (0x%X)\n\r",
            getDeviceIDString(getDeviceID()),
            getDeviceID());

            // Print revision ID
        printf("    Revision ID retrieved from Flash: %s (0x%X)\n\r",
            getRevisionIDString(getRevisionID()),
            getRevisionID());

        terminalTextAttributesReset();

    }

    if (want_wdt) printWatchdogStatus();
    if (want_dmt) printDeadmanStatus();
    if (want_prefetch) printPrefetchStatus();

    if (want_cause_of_reset) {

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

            terminalTextAttributes(RED_COLOR, BLACK_COLOR, BOLD_FONT);

        }

        else {

            terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);

        }

        printf("Cause of most recent device reset: %s\r\n", getResetCauseString(reset_cause));
        terminalTextAttributesReset();

    }

    if (want_up_time) {

        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
        printf("Up time since last device reset: %s\n\r",
                getStringSecondsAsTime(device_on_time_counter));
        terminalTextAttributesReset();

    }

    if (!matched_any) {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Please enter a valid section, or no argument to print everything. Received \"%s\" as section name\r\n", rx_section_name);
        printf("Sections that can be printed include:\r\n"
                "   IDs\r\n"
                "   WDT\r\n"
                "   DMT\r\n"
                "   Prefetch\r\n"
                "   Cause of Reset\r\n"
                "   Up Time\r\n");
        terminalTextAttributesReset();
    }

}

USB_UART_COMMAND(peripheralStatusCommand, "Peripheral Status?",
        "\b\b <peripheral_name>: Prints status of passed host peripheral. Available peripherals:\r\n"
        "       Interrupts\r\n"
        "       Clocks\r\n"
        "       PMD\r\n"
        "       WDT\r\n"
        "       DMT\r\n"
        "       HLVD\r\n"
        "       DDR2\r\n"
        "       Prefetch\r\n"
        "       DMA\r\n"
        "       ADC\r\n"
        "       ADC Channels\r\n"
        "       I2C Master\r\n"
        "       SPI Flash Interface\r\n"
        "       RTCC\r\n"
        "       Timer <x> (x = 1-9)") {
 
    // Snipe out received arguments
    char rx_peripheral_name[32];
    sscanf(input_str, "Peripheral Status? %[^\t\n\r]", rx_peripheral_name);

    // Determine the rail we're enabling or disabling
    if (strcmp(rx_peripheral_name, "Interrupts") == 0) {
        printInterruptStatus();
    }
    else if (strcmp(rx_peripheral_name, "Clocks") == 0) {
        printClockStatus(SYSCLK_INT);
    }
    else if (strcmp(rx_peripheral_name, "PMD") == 0) {
        printPMDStatus();
    }
    else if (strcmp(rx_peripheral_name, "WDT") == 0) {
        printWatchdogStatus();
    }
    else if (strcmp(rx_peripheral_name, "DMT") == 0) {
        printDeadmanStatus();
    }
    else if (strcmp(rx_peripheral_name, "HLVD") == 0) {
        printHLVDStatus();
    }
    else if (strcmp(rx_peripheral_name, "DDR2") == 0) {
        printDDR2Status();
    }
    else if (strcmp(rx_peripheral_name, "Prefetch") == 0) {
       printPrefetchStatus();
    }
    else if (strcmp(rx_peripheral_name, "DMA") == 0) {
        printDMAStatus();
    }
    else if (strcmp(rx_peripheral_name, "ADC Channels") == 0) {
        printADCChannelStatus();
    }
    else if (strcmp(rx_peripheral_name, "ADC") == 0) {
        printADCStatus();
    }
    else if (strcmp(rx_peripheral_name, "RTCC") == 0) {
        printRTCCStatus();
    }
    else if (strcmp(rx_peripheral_name, "I2C Master") == 0) {
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
        printf("I2C Bus Master Controller Status:\r\n");
        I2C_PrintStatus();
    }
    else if (strcmp(rx_peripheral_name, "SPI Flash Interface") == 0) {
        SPI3_PrintStatus();
    }
    else if (strcomp(rx_peripheral_name, "Timer ") == 0) {
        uint32_t read_timer_number;
        sscanf(rx_peripheral_name, "Timer %u", &read_timer_number);
        if (read_timer_number < 1 || read_timer_number > 9) {
            terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("Please enter a timer number between 1 and 9, user entered %u\r\n", read_timer_number);
            terminalTextAttributesReset();
        }
        else {
            printTimerStatus((uint8_t) read_timer_number);
        }
    }
    else {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Please enter a peripheral to view status. Received %s as peripheral name\r\n", rx_peripheral_name);
        printf("Peripherals that can be monitored include:\r\n"
                "   Interrupts\r\n"
                "   Clocks\r\n"
                "   PMD\r\n"
                "   WDT\r\n"
                "   DMT\r\n"
                "   HLVD\r\n"
                "   DDR2\r\n"
                "   ADC\r\n"
                "   ADC Channels\r\n"
                "   Prefetch\r\n"
                "   DMA\r\n"
                "   I2C Master\r\n"
                "   SPI Flash Interface\r\n"
                "   RTCC\r\n"
                "   Timer <x> (x = 1-9)\r\n");
        terminalTextAttributesReset();
    }

}

USB_UART_COMMAND(ddr2SelfTestCommand, "DDR2 Self Test",
        "Runs a DESTRUCTIVE read/write integrity test over all 32MB of DDR2 (data bus, address bus, full-array) and prints pass/fail") {

    (void) input_str;   // no arguments

    terminalTextAttributesReset();
    ddr2SelfTest();
    terminalTextAttributesReset();

}

USB_UART_COMMAND(spiFlashSelfTestCommand, "SPI Flash Self Test",
        "Runs a DESTRUCTIVE read/write/erase integrity test over the last 4KB sector of the SST25VF080B SPI flash and prints pass/fail") {

    (void) input_str;   // no arguments

    terminalTextAttributesReset();
    SST25VF080B_SelfTest();
    terminalTextAttributesReset();

}


USB_UART_COMMAND(errorStatusCommand, "Error Status?", "Prints the status of various error handler flags") {
 
    // Print error handler status
    printErrorHandlerStatus();

    // Print help message
    terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("\n\rCall 'Clear Errors' command to clear any errors that have been set\n\r");
    terminalTextAttributesReset();
    
}

USB_UART_COMMAND(clearErrorsCommand, "Clear Errors", "Clears all error handler flags") {
 
    // Zero out all error handler flags
    clearErrorHandler();

    // Update error LEDs based on error handler status
    update_error_leds_flag = 1;

    terminalTextAttributesReset();
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("Error Handler flags cleared\n\r");
    terminalTextAttributesReset();
    
}

USB_UART_COMMAND(platformStatusCommand, "Platform Status?",
        "\b\b <section>: Prints current state of surrounding circuitry. If no argument is passed, prints everything. Available sections:\r\n"
        "       Revision\r\n"
        "       PGOOD\r\n"
        "       Elapsed Time\r\n"
        "       I2C Slaves\r\n"
        "       SPI Flash") {

    // Snipe out received arguments
    char rx_section_name[32] = {0};
    sscanf(input_str, "Platform Status? %[^\t\n\r]", rx_section_name);

    // No argument means print every section below
    bool print_all = (rx_section_name[0] == '\0');

    bool want_revision = print_all || (strcmp(rx_section_name, "Revision") == 0);
    bool want_pgood     = print_all || (strcmp(rx_section_name, "PGOOD") == 0);
    bool want_elapsed   = print_all || (strcmp(rx_section_name, "Elapsed Time") == 0);
    bool want_i2c       = print_all || (strcmp(rx_section_name, "I2C Slaves") == 0);
    bool want_spiflash  = print_all || (strcmp(rx_section_name, "SPI Flash Device") == 0);
    bool matched_any = want_revision || want_pgood || want_elapsed || want_i2c || want_spiflash;

    if (want_revision) {
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Platform Revision: %s\r\n", PLATFORM_REVISION_STR);
        terminalTextAttributesReset();
    }

    if (want_pgood) printPGOODStatus();

    // Total elapsed on-time and power-cycle count, from the DS1683 total-
    // elapsed-time and event recorder (I2C_DEV_ETR_1) -- broken out as its
    // own section since these two numbers are the ones an operator most
    // often cares about at a glance. DS1683_PrintStatus() (called from
    // I2CDevices_PrintStatus(), the "I2C Slaves" section) still prints the
    // rest of the device's status (command/config registers, alarm flags).
    if (want_elapsed) {

        uint32_t elapsedSeconds;
        uint16_t powerCycleCount;

        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
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

    if (want_i2c) {
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
        printf("\r\nI2C Bus Slave Device Status:\r\n");
        terminalTextAttributesReset();
        I2CDevices_PrintStatus();
    }

    if (want_spiflash) {
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
        printf("\r\nSPI Flash Device Status:\r\n");
        terminalTextAttributesReset();
        SST25VF080B_PrintStatus();
    }

    if (!matched_any) {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Please enter a valid section, or no argument to print everything. Received \"%s\" as section name\r\n", rx_section_name);
        printf("Sections that can be printed include:\r\n"
                "   Revision\r\n"
                "   PGOOD\r\n"
                "   Elapsed Time\r\n"
                "   I2C Slaves\r\n"
                "   SPI Flash Device\r\n");
        terminalTextAttributesReset();
    }

}

USB_UART_COMMAND(liveTelemetryCommand, "Live Telemetry", "Toggles live updates of system level telemetry") {

    terminalTextAttributesReset();

    if (live_telemetry_enable == 0) {
        terminalClearScreen();
        terminalSetCursorHome();
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
        printf("Enabling Live Telemetry\n\r");
        live_telemetry_enable = 1;
    }
    else {
        terminalClearScreen();
        terminalSetCursorHome();
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, BOLD_FONT);
        printf("Disabling Live Telemetry\n\r");
        live_telemetry_enable = 0;
    }

    terminalTextAttributesReset();

}

USB_UART_COMMAND(timeAndDateCommand, "Time and Date?", "Prints the current system time and date") {
 
    terminalTextAttributesReset();
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("Current system time and date:\r\n   ");
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printTimeAndDate();
    terminalTextAttributesReset();
    
}

USB_UART_COMMAND(setRTCCCommand, "Set RTCC:",
        "\b\b <parameter>: <parameter args>: sets a time parameter within the Real Time Clock and Calendar. Available parameters:\r\n"
        "       Date: <mm>/<dd>/<yyyy>: Sets the RTCC date \r\n"
        "       Time: <hh>:<mm>:<ss>: Sets the RTCC time. (Must be 24 hr time format)\r\n"
        "       Weekday: <weekday>: Sets the RTCC weekday\r\n"
        "       Unix Time: <decimal unix time>, <hour offset from UTC to local time>: sets the RTCC to the supplied UNIX time with hour offset from UTC") {

    // Snipe out received arguments
    char rtcc_args[64];
    sscanf(input_str, "Set RTCC: %[^\t\n\r]", rtcc_args);
    
    // only do these things if we actually have arguments for this command
    if (rtcc_args[0]) {

        if(strcomp(rtcc_args, "Date: ") == 0) {

            // Snipe out received string
            uint32_t read_month, read_day, read_year;
            sscanf(rtcc_args, "Date: %12u/%12u/%14u", &read_month, &read_day, &read_year);

            // Write received data into RTCC
            if (read_year >= 2000) {

                rtccWriteDate((uint8_t) read_month, (uint8_t) read_day, (uint16_t) read_year);

                // print out what we just did
                terminalTextAttributesReset();
                terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
                printf("Set RTCC date as %02u/%02u/%04u\r\n", rtcc_shadow.month, rtcc_shadow.day, rtcc_shadow.year);
                terminalTextAttributesReset();

            }

            // return error if year < 2000
            else {

                terminalTextAttributesReset();
                terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
                printf("Enter a valid date after 01/01/2000. User entered %02u/%02u/%04u\r\n", read_month, read_day, read_year);
                terminalTextAttributesReset();

            }

        }

        else if(strcomp(rtcc_args, "Time: ") == 0) {

            // Snipe out received string
            uint32_t read_hour, read_minute, read_second;
            sscanf(rtcc_args, "Time: %12u:%12u:%12u", &read_hour, &read_minute, &read_second);

            if (read_hour < 24 && read_minute < 60 && read_second < 60) {
                rtccWriteTime((uint8_t) read_hour, (uint8_t) read_minute, (uint8_t) read_second);
            }
            
            // print out what we just did
            terminalTextAttributesReset();
            terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("Set RTCC time as %02u:%02u:%02u (24 hr time format)\r\n", rtcc_shadow.hours, rtcc_shadow.minutes, rtcc_shadow.seconds);
            terminalTextAttributesReset();

        }

        else if (strcomp(rtcc_args, "Weekday: ") == 0) {

            char read_weekday[16];
            uint8_t read_weekday_enum;
            sscanf(rtcc_args, "Weekday: %s", &read_weekday);

            if (strcmp(read_weekday, "Sunday") == 0) read_weekday_enum = 0;
            else if (strcmp(read_weekday, "Monday") == 0) read_weekday_enum = 1;
            else if (strcmp(read_weekday, "Tuesday") == 0) read_weekday_enum = 2;
            else if (strcmp(read_weekday, "Wednesday") == 0) read_weekday_enum = 3;
            else if (strcmp(read_weekday, "Thursday") == 0) read_weekday_enum = 4;
            else if (strcmp(read_weekday, "Friday") == 0) read_weekday_enum = 5;
            else if (strcmp(read_weekday, "Saturday") == 0) read_weekday_enum = 6;
            else read_weekday_enum = 255;

            if (read_weekday_enum != 255) {

                // print out what we just did
                terminalTextAttributesReset();
                terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
                rtccWriteWeekday(read_weekday_enum);
                printf("Set RTCC weekday as %s\r\n", getDayOfWeek(rtcc_shadow.weekday));
                terminalTextAttributesReset();

            }

            else {

                terminalTextAttributesReset();
                terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
                printf("Please enter a valid day of the weekday. Input is case sensitive, user entered %s\r\n", read_weekday);
                terminalTextAttributesReset();

            }

        }

        else if (strcomp(rtcc_args, "Unix Time: ") == 0) {

            // Snipe out received string
            uint32_t read_unix_time, read_offset;
            sscanf(rtcc_args, "Unix Time: %lu, %d", &read_unix_time, &read_offset);

            // remove timezone from unix time (this converts from UTC to local time)
            read_offset *= 3600;                // convert from hours to seconds
            read_unix_time += read_offset;      // add or remove these seconds to read unix time

            // write unix time into RTCC
            rtccWriteUnixTime(read_unix_time);

            // print out what we just did
            terminalTextAttributesReset();
            terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("Set RTCC time as %02u:%02u:%02u\r\n", rtcc_shadow.hours, rtcc_shadow.minutes, rtcc_shadow.seconds);
            printf("Set RTCC date as %02u/%02u/%04u\r\n", rtcc_shadow.month, rtcc_shadow.day, rtcc_shadow.year);
            printf("Set RTCC weekday as %s\r\n", getDayOfWeek(rtcc_shadow.weekday));
            terminalTextAttributesReset();

        }

        else {
     
            terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("Please enter an RTCC parameter to set, as well as what that parameter should be set to\r\n");
            printf("Available parameters are:\r\n");
            printf("    Date: <mm>/<dd>/<yyyy>: Sets the RTCC date \r\n");
            printf("    Time: <hh>:<mm>:<ss>: Sets the RTCC time. (Must be 24 hr time)\r\n");
            printf("    Weekday: <weekday>: Sets the RTCC weekday\r\n");
            printf("    Unix Time: <decimal unix time>, <hour offset from UTC to local time>: sets the RTCC to the supplied UNIX time with hour offset from UTC\r\n");
            terminalTextAttributesReset();

        }

    }
        
    else {
     
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Please enter an RTCC parameter to set, as well as what that parameter should be set to\r\n");
        printf("Available parameters are:\r\n");
        printf("    Date: <mm>/<dd>/<yyyy>: Sets the RTCC date \r\n");
        printf("    Time: <hh>:<mm>:<ss>: Sets the RTCC time. (Must be 24 hr time)\r\n");
        printf("    Weekday: <weekday>: Sets the RTCC weekday\r\n");
        printf("    Unix Time: <decimal unix time>, <hour offset from UTC to local time>: sets the RTCC to the supplied UNIX time with hour offset from UTC\r\n");
        terminalTextAttributesReset();
        
    }
    
}

USB_UART_COMMAND(flirPowerOnCommand, "FLIR Power On",
        "Enables the FLIR 1.2V and 2.8V power supplies and blocks until their PGOOD signals go high") {

    terminalTextAttributesReset();
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("Enabling FLIR power supplies...\r\n");

    // Turn on the 1.2V and 2.8V power supplies used by the FLIR module
    POS1P2_RUN_PIN = HIGH;
    POS2P8_RUN_PIN = HIGH;

    // Block on the 1.2V supply and print its status once it's good
    while (POS1P2_PGOOD_PIN == LOW);
    printf("    +1.2V supply PGOOD is high\r\n");

    // Block on the 2.8V supply and print its status once it's good
    while (POS2P8_PGOOD_PIN == LOW);
    printf("    +2.8V supply PGOOD is high\r\n");

    FLIR_CLK_EN_PIN = HIGH;
    printf("    FLIR Clock Enabled\r\n");
    
    nFLIR_PWR_DWN_PIN = HIGH;
    printf("    FLIR PWR Down Signal de-asserted\r\n");
    
    nFLIR_RESET_PIN = HIGH;
    printf("    FLIR Reset signal de-asserted\r\n");
    
    printf("FLIR power supplies enabled, all PGOOD signals are high\r\n");
    terminalTextAttributesReset();

}

USB_UART_COMMAND(flirPowerOffCommand, "FLIR Power Off", "Disables the FLIR 1.2V and 2.8V power supplies") {

    // Turn off the 1.2V and 2.8V power supplies used by the FLIR module
    POS1P2_RUN_PIN = LOW;
    POS2P8_RUN_PIN = LOW;

    terminalTextAttributesReset();
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("FLIR power supplies disabled\r\n");
    terminalTextAttributesReset();

}

USB_UART_COMMAND(eraseSPIFlash, "Erase SPI Flash", "Erases the entire SPI Flash memory") {

    bool success = SST25VF080B_EraseChip();

    terminalTextAttributesReset();
    if (success) {
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("SPI Flash erased successfully\r\n");
    } else {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Failed to erase SPI Flash\r\n");
    }

    terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("Note: The SST25VF080B device has limited write endurance, please use sparingly.\r\n");
    terminalTextAttributesReset();

}