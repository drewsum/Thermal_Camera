
#include <xc.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "usb_uart_rx_lookup_table.h"
#include "usb_uart.h"
#include "uthash.h"

#include "main.h"
#include "rtcc.h"
#include "error_handler.h"
#include "heartbeat_services.h"
#include "cause_of_reset.h"
#include "terminal_control.h"
#include "device_control.h"
#include "watchdog_timer.h"
#include "usb_uart.h"
#include "prefetch.h"
#include "power_saving.h"
#include "pin_macros.h"
#include "pgood_monitor.h"
#include "telemetry.h"
#include "plib_i2c.h"
#include "plib_i2c_master.h"
#include "adc.h"
#include "adc_channels.h"

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

USB_UART_COMMAND(idnCommand, "*IDN?", "Prints identification string") {
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

USB_UART_COMMAND(hostStatusCommand, "Host Status?", "Prints status of MCU host device (IDs, WDT, DMT, Prefetch, Cause of Reset, up time)") {

    terminalTextAttributesReset();
    
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("Host Firmware Version: %s\r\n", FIRMWARE_VERSION_STR);
    
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("Host Device IDs:\r\n");
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
    
    printWatchdogStatus();
    printDeadmanStatus();
    printPrefetchStatus();

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

    terminalTextAttributesReset();
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("Up time since last device reset: %s\n\r",
            getStringSecondsAsTime(device_on_time_counter));
    terminalTextAttributesReset();

}

USB_UART_COMMAND(peripheralStatusCommand, "Peripheral Status?",
        "\b\b <peripheral_name>: Prints status of passed host peripheral. Available peripherals:\r\n"
        "       Interrupts\r\n"
        "       Clocks\r\n"
        "       PMD\r\n"
        "       WDT\r\n"
        "       DMT\r\n"
        "       Prefetch\r\n"
        "       DMA\r\n"
        "       ADC\r\n"
        "       ADC Channels\r\n"
        "       I2C Master\r\n"
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
        printI2CMasterStatus();
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
                "   ADC\r\n"
                "   ADC Channels\r\n"
                "   Prefetch\r\n"
                "   DMA\r\n"
                "   I2C Master\r\n"
                "   RTCC\r\n"
                "   Timer <x> (x = 1-9)\r\n");
        terminalTextAttributesReset();
    }

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
        "Prints current state of surrounding circuitry, including PGOOD, time of flight, I2C slaves") {
 
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("Platform Revision: %s\r\n", PLATFORM_REVISION_STR);
    
    printPGOODStatus();

    terminalTextAttributesReset();
    
//    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
//    printf("\r\nI2C Bus Slave Device Status:\r\n");
//    terminalTextAttributesReset();
//    printTemperatureSensorStatus();
//    printPowerMonitorStatus();
//    miscI2CDevicesPrintStatus();
    
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

