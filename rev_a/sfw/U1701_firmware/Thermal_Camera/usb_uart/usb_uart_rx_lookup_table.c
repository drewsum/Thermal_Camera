
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
#include "gpio/pic32mzda_gpio_setup.h"
#include "application/pgood_monitor.h"
#include "application/battery_monitor.h"
#include "application/telemetry.h"
#include "i2c/i2c_master.h"
#include "adc/adc.h"
#include "application/adc_channels.h"
#include "core/hlvd.h"
#include "core/ddr2.h"
#include "i2c/i2c_devices.h"
#include "spi/spi3.h"
#include "spi/device_driver/w25q128jv.h"
#include "spi/device_driver/w25q128jv_disk.h"
#include "spi/flash_fileio.h"
#include "sdhc/sdhc.h"
#include "sdhc/device_driver/sd_card.h"
#include "sdhc/sd_fileio.h"
#include "usb/usb.h"
#include "usb/device_driver/usb_msd.h"
#include "glcd/glcd.h"
#include "gui/gui.h"
#include "application/backlight_pwm.h"
#include "application/image_loader.h"
#include "application/flir/flir.h"
#include "application/flir/flir_process.h"
#include "application/flir/flir_vospi.h"

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

USB_UART_COMMAND(sleepCommand, "Sleep", "Enters low-power sleep so the fuel gauge can take an open-circuit reading (press RESET to exit)") {

    // Everything about this lives in application/power_saving.c -- see
    // enterLowPowerSleep() for what gets shut down and why. Does not return.
    enterLowPowerSleep();

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
        "       GPIO Ports\r\n"
        "       I2C Master\r\n"
        "       SPI Flash Interface\r\n"
        "       SDHC\r\n"
        "       USB\r\n"
        "       GLCD\r\n"
        "       GUI\r\n"
        "       FLIR SPI\r\n"
        "       Backlight PWM\r\n"
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
    else if (strcmp(rx_peripheral_name, "GPIO Ports") == 0) {
        printGPIOPortsStatus();
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
    else if (strcmp(rx_peripheral_name, "SDHC") == 0) {
        SDHC_PrintStatus();
    }
    else if (strcmp(rx_peripheral_name, "USB") == 0) {
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
        printf("USB Module Status:\r\n");
        USB_PrintStatus();
    }
    else if (strcmp(rx_peripheral_name, "GLCD") == 0) {
        GLCD_PrintStatus();
    }
    else if (strcmp(rx_peripheral_name, "GUI") == 0) {
        GUI_PrintStatus();
    }
    else if (strcmp(rx_peripheral_name, "FLIR SPI") == 0) {
        FLIR_VOSPI_PrintStatus();
    }
    else if (strcmp(rx_peripheral_name, "Backlight PWM") == 0) {
        BacklightPWM_PrintStatus();
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
                "   GPIO Ports\r\n"
                "   Prefetch\r\n"
                "   DMA\r\n"
                "   I2C Master\r\n"
                "   SPI Flash Interface\r\n"
                "   SDHC\r\n"
                "   USB\r\n"
                "   GLCD\r\n"
                "   Backlight PWM\r\n"
                "   RTCC\r\n"
                "   Timer <x> (x = 1-9)\r\n");
        terminalTextAttributesReset();
    }

}

USB_UART_COMMAND(setBacklightBrightnessCommand, "Set Backlight Brightness:",
        "\b\b <percent>: Sets the LCD backlight PWM brightness, 0-100 (percent)") {

    uint32_t read_percent;

    if (sscanf(input_str, "Set Backlight Brightness: %u", &read_percent) != 1
            || read_percent > 100) {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Please enter a brightness percentage between 0 and 100\r\n");
        terminalTextAttributesReset();
        return;
    }

    BacklightPWM_SetBrightness((uint8_t) read_percent);

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("LCD Backlight brightness set to %lu%%\r\n", (unsigned long) read_percent);
    terminalTextAttributesReset();

}

USB_UART_COMMAND(displayImageCommand, "Display Image:",
        "\b\b <media>, <filename>: Decodes a PNG from storage into the LCD frame buffer.\r\n"
        "       media: Flash (SPI flash volume) or SD (microSD card volume)\r\n"
        "       filename: 8.3 short filename, e.g. TEST.PNG (image must be 320x240)") {

    char media_str[16] = {0};
    char filename_str[64] = {0};

    if (sscanf(input_str, "Display Image: %15[^,], %63[^\t\n\r]", media_str, filename_str) != 2) {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Usage: Display Image: <media>, <filename> (media: Flash or SD)\r\n");
        terminalTextAttributesReset();
        return;
    }

    IMAGE_MEDIA media;
    if (strcmp(media_str, "Flash") == 0) {
        media = IMAGE_MEDIA_SPI_FLASH;
    } else if (strcmp(media_str, "SD") == 0) {
        media = IMAGE_MEDIA_SD_CARD;
    } else {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Unknown media \"%s\" -- use Flash or SD\r\n", media_str);
        terminalTextAttributesReset();
        return;
    }

    // ImageLoader_DisplayPNG() prints its own success/failure diagnostics
    ImageLoader_DisplayPNG(media, filename_str);

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
        "       Battery\r\n"
        "       Elapsed Time\r\n"
        "       I2C Slaves\r\n"
        "       SPI Flash Device\r\n"
        "       USB Device") {

    // Snipe out received arguments
    char rx_section_name[32] = {0};
    sscanf(input_str, "Platform Status? %[^\t\n\r]", rx_section_name);

    // No argument means print every section below
    bool print_all = (rx_section_name[0] == '\0');

    bool want_revision = print_all || (strcmp(rx_section_name, "Revision") == 0);
    bool want_pgood     = print_all || (strcmp(rx_section_name, "PGOOD") == 0);
    bool want_battery   = print_all || (strcmp(rx_section_name, "Battery") == 0);
    bool want_elapsed   = print_all || (strcmp(rx_section_name, "Elapsed Time") == 0);
    bool want_i2c       = print_all || (strcmp(rx_section_name, "I2C Slaves") == 0);
    bool want_spiflash  = print_all || (strcmp(rx_section_name, "SPI Flash Device") == 0);
    bool want_usb       = print_all || (strcmp(rx_section_name, "USB Device") == 0);
    bool matched_any = want_revision || want_pgood || want_battery || want_elapsed || want_i2c || want_spiflash || want_usb;

    if (want_revision) {
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Platform Revision: %s\r\n", PLATFORM_REVISION_STR);
        terminalTextAttributesReset();
    }

    if (want_pgood) printPGOODStatus();

    if (want_battery) printBatteryStatus();

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
        W25Q128JV_PrintStatus();
    }

    if (want_usb) {
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
        printf("\r\nUSB Mass Storage Device Status:\r\n");
        terminalTextAttributesReset();
        USB_MSD_PrintStatus();
    }

    if (!matched_any) {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Please enter a valid section, or no argument to print everything. Received \"%s\" as section name\r\n", rx_section_name);
        printf("Sections that can be printed include:\r\n"
                "   Revision\r\n"
                "   PGOOD\r\n"
                "   Battery\r\n"
                "   Elapsed Time\r\n"
                "   I2C Slaves\r\n"
                "   SPI Flash Device\r\n"
                "   USB Device\r\n");
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

        // Nothing is on screen yet, so the first refresh can't diff against
        // whatever the shadow was left holding by the previous session
        live_telemetry_full_repaint = 1;
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

USB_UART_COMMAND(flirStreamOnCommand, "FLIR Stream On",
        "Starts thermal video capture (VoSPI) onto GLCD Layer 0. The sensor is already powered and configured from boot, so this takes effect immediately.") {

    terminalTextAttributesReset();
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);

    if (FLIR_StreamOn()) {
        // Capture arms ~200ms from now: VoSPI packet alignment is set by where
        // clocking starts, and the sensor only restarts on a packet boundary
        // after /CS has been idle for ~185ms (see flir_vospi.c).
        printf("Thermal video starting -- capture arms after the ~200ms VoSPI sync window\r\n");
    } else {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Cannot stream: the Lepton is not booted and configured -- see 'FLIR Status?'\r\n");
        printf("(If it is powered off, 'FLIR Power On' brings it back.)\r\n");
    }

    terminalTextAttributesReset();

}

USB_UART_COMMAND(flirStreamOffCommand, "FLIR Stream Off",
        "Stops thermal video capture and blanks the video layer. The sensor stays powered, booted, and configured, so 'FLIR Stream On' resumes instantly.") {

    FLIR_StreamOff();

    terminalTextAttributesReset();
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("Thermal video stopped (sensor still powered and configured)\r\n");
    terminalTextAttributesReset();

}

USB_UART_COMMAND(flirPowerOnCommand, "FLIR Power On",
        "Re-powers the FLIR Lepton 3.5 after a 'FLIR Power Off' (rails -> master clock -> reset release, then the CCI configuration). The sensor is already powered at boot, so this is only needed after powering it down. Video stays idle until 'FLIR Stream On'.") {

    terminalTextAttributesReset();
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);

    // FLIR_PowerOn() runs the ordered rails/clock/reset sequence with bounded
    // PGOOD waits; the long boot + CCI handshake then finish asynchronously in
    // FLIR_Tasks() so the console stays responsive. Check "FLIR Status?".
    if (FLIR_PowerOn()) {
        printf("FLIR power-up sequence started -- camera booting (~950ms).\r\n");
        printf("Use 'FLIR Status?' to watch it reach READY, then 'FLIR Stream On'.\r\n");
    } else {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("FLIR power-up FAILED (a rail did not reach PGOOD) -- see 'Error Handler Status?'\r\n");
    }

    terminalTextAttributesReset();

}

USB_UART_COMMAND(flirPowerOffCommand, "FLIR Power Off",
        "Debug only: powers the FLIR Lepton fully down (held in reset, clock gated, both rails off). WARNING -- an unpowered Lepton holds I2C1 low, breaking every other device on the bus. Use 'FLIR Stream Off' to just stop the video.") {

    FLIR_PowerOff();

    terminalTextAttributesReset();
    terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("FLIR Lepton powered down\r\n");
    printf("WARNING: with its rails down the module clamps SDA/SCL low -- every\r\n");
    printf("         other I2C1 device is unreachable until 'FLIR Power On'.\r\n");
    terminalTextAttributesReset();

}

USB_UART_COMMAND(flirStatusCommand, "FLIR Status?",
        "Prints FLIR Lepton module status: state machine, control-signal/rail levels, CCI status and temperatures, capture health, and the active palette/AGC window. (The SPI4/DMA/INT1 MCU peripheral settings are under 'Peripheral Status? FLIR SPI'.)") {

    terminalTextAttributesReset();
    FLIR_PrintStatus();
    terminalTextAttributesReset();

}

USB_UART_COMMAND(flirPacketDumpCommand, "FLIR Packet Dump",
        "Dumps the leading bytes of the last few VoSPI packets the DMA delivered, decoded. Use this when 'FLIR Status?' shows no frames: it separates a dead SPI link (all FF/00) from a live link carrying only discard packets from a working link that has lost byte alignment.") {

    terminalTextAttributesReset();
    FLIR_VOSPI_PrintPacketDump();
    terminalTextAttributesReset();

}

USB_UART_COMMAND(flirPaletteCommand, "FLIR Palette:",
        "\b\b <palette>: Selects the thermal color palette. Options: Ironbow, White Hot, Black Hot, Rainbow, Rainbow HC, Arctic, Lava, Glowbow") {

    char palette_str[24] = {0};
    FLIR_PALETTE p;

    terminalTextAttributesReset();

    if (sscanf(input_str, "FLIR Palette: %23[^\t\n\r]", palette_str) != 1) {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Usage: FLIR Palette: <Ironbow|White Hot|Black Hot|Rainbow|Rainbow HC|Arctic|Lava|Glowbow>\r\n");
        terminalTextAttributesReset();
        return;
    }

    // Matched against FLIRProcess_PaletteName() so the option list can only
    // ever name palettes the driver actually builds.
    for (p = (FLIR_PALETTE)0; p < FLIR_PALETTE_COUNT; p++) {
        if (strcmp(palette_str, FLIRProcess_PaletteName(p)) == 0) {
            FLIR_SetPalette(p);

            // Wakes GUI_Tasks() on the very next main-loop pass rather than
            // waiting for heartbeatServices()'s 500ms tick, so the home
            // screen's palette scale (gui/screens/screen_home.c) repaints
            // right away instead of lagging the command by up to half a
            // second.
            gui_refresh_request = 1;

            terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("Palette set to %s\r\n", FLIRProcess_PaletteName(p));
            terminalTextAttributesReset();
            return;
        }
    }

    terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("Unknown palette '%s' (options: Ironbow, White Hot, Black Hot, Rainbow, Rainbow HC, Arctic, Lava, Glowbow)\r\n", palette_str);
    terminalTextAttributesReset();

}

USB_UART_COMMAND(clearImageCommand, "Clear Image",
        "Hides the still-image layer (GLCD Layer 2) loaded by 'Display Image:', revealing the thermal video and GUI again") {

    ImageLoader_Clear();

    terminalTextAttributesReset();
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("Still-image layer hidden\r\n");
    terminalTextAttributesReset();

}

USB_UART_COMMAND(flashFormatCommand, "Flash Format",
        "DESTRUCTIVELY erases the entire SPI Flash chip, then re-formats and remounts its FAT volume (FAT/superfloppy, labeled THERMAL SPI)") {

    terminalTextAttributesReset();

    // The erase happens underneath the FAT volume -- unmount first so
    // FatFs holds no stale state (also refuses while a USB host owns the
    // media, which an erase-under-the-host absolutely must not bypass)
    if (usb_msd_media_owned_by_host) {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("SPI flash is owned by the USB host -- unplug USB or send 'USB Detach' first\r\n");
        terminalTextAttributesReset();
        return;
    }

    if (W25Q128JV_WriteProtectIsEnabled()) {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("SPI flash write protect is enabled -- run \"Flash Write Protect: Off\" first\r\n");
        terminalTextAttributesReset();
        return;
    }

    FlashFileIO_Unmount();

    if (!W25Q128JV_EraseChip()) {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Failed to erase SPI Flash -- FAT volume was NOT reformatted\r\n");
        terminalTextAttributesReset();
        return;
    }

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("SPI Flash erased successfully\r\n");

    if (FlashFileIO_Format()) {
        printf("SPI flash FAT volume formatted and remounted\r\n");
    } else {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Failed to format SPI flash FAT volume\r\n");
    }

    terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("Note: The W25Q128JV device has limited write endurance, please use sparingly.\r\n");
    terminalTextAttributesReset();

}

// Callback for SDFileIO_ListFiles() -- prints one directory entry per line
static void printSDFileLine(const char *line) {
    printf("    %s\r\n", line);
}

USB_UART_COMMAND(sdCardInfoCommand, "SD Card Info?",
        "Prints CID/CSD-derived microSD card metadata (manufacturer, capacity, type) and mounted FAT volume info") {

    (void) input_str;   // no arguments

    terminalTextAttributesReset();

    if (SD_Card_GetInfo() == NULL) {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("No microSD card currently initialized\r\n");
        terminalTextAttributesReset();
        return;
    }

    SD_Card_PrintInfo();

    char fsType[8], label[16];
    uint32_t totalKB, freeKB;
    if (SDFileIO_GetVolumeInfo(fsType, sizeof(fsType), label, sizeof(label), &totalKB, &freeKB, NULL, NULL)) {
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    Filesystem: %s\r\n", fsType);
        printf("    Volume Label: %s\r\n", label[0] ? label : "(none)");
        printf("    Total Space: %lu KB\r\n", (unsigned long) totalKB);
        printf("    Free Space: %lu KB\r\n", (unsigned long) freeKB);
    } else {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    No FAT volume currently mounted\r\n");
    }

    terminalTextAttributesReset();

}

USB_UART_COMMAND(sdListFilesCommand, "SD List Files",
        "\b\b <path>: Lists files in the given directory on the mounted microSD card (defaults to the root directory if omitted)") {

    char rx_path[64] = "";
    sscanf(input_str, "SD List Files %[^\t\n\r]", rx_path);

    terminalTextAttributesReset();

    if (SD_Card_GetInfo() == NULL) {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("No microSD card mounted\r\n");
        terminalTextAttributesReset();
        return;
    }

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("Contents of %s:\r\n", rx_path[0] ? rx_path : "/");
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);

    if (!SDFileIO_ListFiles(rx_path[0] ? rx_path : NULL, printSDFileLine)) {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Failed to open directory\r\n");
    }

    terminalTextAttributesReset();

}

USB_UART_COMMAND(sdEjectCommand, "SD Eject",
        "Unmounts the microSD card's FAT volume and powers it down so it can be safely removed") {

    (void) input_str;   // no arguments

    terminalTextAttributesReset();

    if (SD_Card_GetInfo() == NULL) {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("No microSD card currently mounted\r\n");
        terminalTextAttributesReset();
        return;
    }

    if (SDFileIO_Unmount()) {
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("microSD card unmounted and powered down -- safe to remove\r\n");
    } else {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Failed to unmount microSD card\r\n");
    }

    terminalTextAttributesReset();

}

USB_UART_COMMAND(usbStatusCommand, "USB Status?",
        "Prints the USB mass storage device's bus state (speed, address, configuration) and transport/LUN status") {

    (void) input_str;   // no arguments

    terminalTextAttributesReset();
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("USB Module Status:\r\n");
    USB_PrintStatus();

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("USB Mass Storage Status:\r\n");
    USB_MSD_PrintStatus();

}

USB_UART_COMMAND(usbDetachCommand, "USB Detach",
        "Soft-disconnects from the USB host (host sees an unplug) and remounts the SD/FLASH volumes for local use") {

    (void) input_str;   // no arguments

    terminalTextAttributesReset();

    USB_Detach();

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("USB detached (soft disconnect) -- send \"USB Attach\" to re-present to the host\r\n");
    terminalTextAttributesReset();

}

USB_UART_COMMAND(usbAttachCommand, "USB Attach",
        "Re-presents the USB mass storage device to the host after a \"USB Detach\"") {

    (void) input_str;   // no arguments

    terminalTextAttributesReset();

    USB_Attach();

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("USB attached -- the host will re-enumerate if a cable is connected\r\n");
    terminalTextAttributesReset();

}

USB_UART_COMMAND(flashFsInfoCommand, "Flash FS Info?",
        "Prints the SPI flash FAT volume's filesystem type, label, and total/free space") {

    (void) input_str;   // no arguments

    terminalTextAttributesReset();

    char fsType[8], label[16];
    uint32_t totalKB, freeKB;
    if (FlashFileIO_GetVolumeInfo(fsType, sizeof(fsType), label, sizeof(label), &totalKB, &freeKB, NULL, NULL)) {
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
        printf("SPI Flash FAT Volume:\r\n");
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    Filesystem: %s\r\n", fsType);
        printf("    Volume Label: %s\r\n", label[0] ? label : "(none)");
        printf("    Total Space: %lu KB\r\n", (unsigned long) totalKB);
        printf("    Free Space: %lu KB\r\n", (unsigned long) freeKB);
        W25Q128JV_Disk_PrintStatus();
    } else {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("SPI flash FAT volume not currently mounted\r\n");
    }

    terminalTextAttributesReset();

}

// Prints one volume's total/used/free space in bytes and percent for
// storageUsageCommand below. `usedPercentColor` highlights the Used line
// yellow past 90% full, green otherwise -- an early warning, not an error.
static void printStorageUsageLine(uint32_t totalBytes, uint32_t freeBytes) {

    uint32_t usedBytes = totalBytes - freeBytes;

    // Tenths of a percent (e.g. 423 -> "42.3%"), avoiding float and the
    // 32-bit overflow a plain "usedBytes * 1000" risks on a multi-GB card
    uint32_t usedPermille = (totalBytes > 0)
            ? (uint32_t)(((uint64_t) usedBytes * 1000u) / totalBytes) : 0;
    uint32_t freePermille = 1000u - usedPermille;

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Total: %10lu bytes (%lu KB)\r\n", (unsigned long) totalBytes, (unsigned long) (totalBytes / 1024u));

    terminalTextAttributes((usedPermille >= 900u) ? YELLOW_COLOR : GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Used:  %10lu bytes (%lu.%lu%%)\r\n", (unsigned long) usedBytes,
            (unsigned long) (usedPermille / 10u), (unsigned long) (usedPermille % 10u));

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Free:  %10lu bytes (%lu.%lu%%)\r\n", (unsigned long) freeBytes,
            (unsigned long) (freePermille / 10u), (unsigned long) (freePermille % 10u));

}

// Prints one labeled region's byte count and its percentage of `totalBytes`
// -- used for the DDR2 known-reservations breakdown below, where there's
// no "free" concept (DDR2 has no allocator, just documented firmware
// reservations), only a proportion of the whole.
static void printStorageReservationLine(const char *label, uint32_t bytes, uint32_t totalBytes) {

    uint32_t permille = (totalBytes > 0)
            ? (uint32_t)(((uint64_t) bytes * 1000u) / totalBytes) : 0;

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    %-22s%10lu bytes (%lu.%lu%%)\r\n", label, (unsigned long) bytes,
            (unsigned long) (permille / 10u), (unsigned long) (permille % 10u));

}

// Linker-provided symbols (PIC32MZ2064DAR176 device-specific linker
// script): _end marks the first free byte after all linked .data/.bss
// (i.e. total static RAM usage from the region base); _min_heap_size is
// this project's own "--defsym=_min_heap_size=115200" build flag
// (cmake/Thermal_Camera/default/user.cmake) echoed back by the linker.
// Neither is an actual variable -- per the standard GNU linker-symbol
// idiom, the SYMBOL'S ADDRESS is the value. Declared as arrays-of-unknown-
// size (not "extern uint32_t x;" + "&x") because MIPS/XC32 tries to
// access a plain scalar extern via GP-relative (small-data) addressing,
// which only encodes a tiny offset window and fails to link
// ("relocation truncated to fit: R_MIPS_GPREL16") once the symbol's
// linked value exceeds it -- the array form sidesteps that code path,
// and the identifier itself (no "&") already decays to the address.
extern uint32_t _end[];
extern uint32_t _min_heap_size[];

USB_UART_COMMAND(storageUsageCommand, "Storage Usage?",
        "Prints consumed vs. total space, in bytes and percent, for every mounted FAT volume (microSD card and SPI flash), plus internal SRAM/Flash and DDR2 SDRAM utilization") {

    (void) input_str;   // no arguments

    terminalTextAttributesReset();

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("microSD Card (0:):\r\n");
    {
        uint32_t totalBytes, freeBytes;
        if (SDFileIO_GetVolumeInfo(NULL, 0, NULL, 0, NULL, NULL, &totalBytes, &freeBytes)) {
            printStorageUsageLine(totalBytes, freeBytes);
        } else {
            terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("    Not currently mounted\r\n");
        }
    }

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("SPI Flash (1:):\r\n");
    {
        uint32_t totalBytes, freeBytes;
        if (FlashFileIO_GetVolumeInfo(NULL, 0, NULL, 0, NULL, NULL, &totalBytes, &freeBytes)) {
            printStorageUsageLine(totalBytes, freeBytes);
        } else {
            terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("    Not currently mounted\r\n");
        }
    }

    // Internal SRAM: static (.data+.bss) usage is read live from the
    // linker-provided _end symbol; heap and stack are NOT independently
    // trackable at runtime with this toolchain's C library (no mallinfo()-
    // style introspection is exposed), so both are reported as the fixed
    // capacity the linker's best-fit allocator reserved for them at link
    // time (per p32MZ2064DAR176.ld: "heap and stack are best-fit allocated
    // ... after other data and bss sections" -- heap gets exactly
    // _min_heap_size, stack gets whatever's left over) rather than a true
    // current-usage figure.
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("Internal SRAM:\r\n");
    {
        uint32_t staticBytes = (uint32_t) _end - MCU_SRAM_BASE_ADDRESS;
        uint32_t heapReservedBytes = (uint32_t) _min_heap_size;
        uint32_t stackReservedBytes = MCU_SRAM_TOTAL_BYTES - staticBytes - heapReservedBytes;

        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    Total: %10lu bytes (%lu KB)\r\n",
                (unsigned long) MCU_SRAM_TOTAL_BYTES, (unsigned long) (MCU_SRAM_TOTAL_BYTES / 1024u));
        printStorageReservationLine("Static (.data+.bss):", staticBytes, MCU_SRAM_TOTAL_BYTES);
        printStorageReservationLine("Heap (reserved):", heapReservedBytes, MCU_SRAM_TOTAL_BYTES);
        printStorageReservationLine("Stack (reserved):", stackReservedBytes, MCU_SRAM_TOTAL_BYTES);
    }

    // Internal program Flash: this device's linker script exposes no
    // symbol marking the end of used flash (unlike _end for RAM, above),
    // so only total capacity is available here -- not a live "used" figure.
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("Internal Program Flash:\r\n");
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Total: %10lu bytes (%lu KB)\r\n",
            (unsigned long) MCU_FLASH_TOTAL_BYTES, (unsigned long) (MCU_FLASH_TOTAL_BYTES / 1024u));
    terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Used: not available (no linker symbol exposes this on this device)\r\n");

    // DDR2 SDRAM: there is no allocator over this memory (core/ddr2.h),
    // just a small number of fixed, documented firmware reservations --
    // this reports those known reservations, not a true live "used" figure
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("DDR2 SDRAM (known firmware reservations, not a live allocator):\r\n");
    {
        // Full map (and the rationale for each) is in gui/gui.h. The LVGL
        // heap is the one entry with a live utilization figure -- see
        // "Peripheral Status? GUI" -- since PNG decodes share it.
        uint32_t reservedBytes = GLCD_FRAMEBUFFER_SIZE_BYTES
                + (2u * GLCD_OVERLAY_SIZE_BYTES)
                + GUI_LVGL_HEAP_SIZE_BYTES
                + GLCD_LAYER2_SIZE_BYTES
                + (2u * FLIR_VOSPI_FRAME_SIZE_BYTES);

        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    Total: %10lu bytes (%lu KB)\r\n",
                (unsigned long) DDR2_SIZE_BYTES, (unsigned long) (DDR2_SIZE_BYTES / 1024u));
        // Labels stay within printStorageReservationLine()'s %-22s column
        printStorageReservationLine("GLCD Frame Buffer:", GLCD_FRAMEBUFFER_SIZE_BYTES, DDR2_SIZE_BYTES);
        printStorageReservationLine("GUI Overlay Buffer A:", GLCD_OVERLAY_SIZE_BYTES, DDR2_SIZE_BYTES);
        printStorageReservationLine("GUI Overlay Buffer B:", GLCD_OVERLAY_SIZE_BYTES, DDR2_SIZE_BYTES);
        printStorageReservationLine("LVGL Heap:", GUI_LVGL_HEAP_SIZE_BYTES, DDR2_SIZE_BYTES);
        printStorageReservationLine("GLCD Layer 2 Image:", GLCD_LAYER2_SIZE_BYTES, DDR2_SIZE_BYTES);
        printStorageReservationLine("FLIR VoSPI Frames (x2):", 2u * FLIR_VOSPI_FRAME_SIZE_BYTES, DDR2_SIZE_BYTES);
        printStorageReservationLine("Unreserved:", DDR2_SIZE_BYTES - reservedBytes, DDR2_SIZE_BYTES);
    }

    terminalTextAttributesReset();

}

USB_UART_COMMAND(flashListFilesCommand, "Flash List Files",
        "\b\b <path>: Lists files in the given directory on the SPI flash FAT volume (defaults to the root directory if omitted)") {

    char rx_path[64] = "";
    sscanf(input_str, "Flash List Files %[^\t\n\r]", rx_path);

    terminalTextAttributesReset();
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("Contents of flash %s:\r\n", rx_path[0] ? rx_path : "/");
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);

    if (!FlashFileIO_ListFiles(rx_path[0] ? rx_path : NULL, printSDFileLine)) {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Failed to open directory (volume mounted?)\r\n");
    }

    terminalTextAttributesReset();

}

USB_UART_COMMAND(flashWriteProtectQueryCommand, "Flash Write Protect?",
        "Prints whether the SPI flash hardware write protect is currently enabled") {

    (void) input_str;   // no arguments

    terminalTextAttributesReset();
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("SPI flash write protect is currently %s\r\n",
            W25Q128JV_WriteProtectIsEnabled() ? "ENABLED" : "DISABLED");
    terminalTextAttributesReset();

}

USB_UART_COMMAND(flashWriteProtectCommand, "Flash Write Protect:",
        "\b\b <On|Off>: Sets the SPI flash hardware write protect (block-protect bits locked by the WP# pin). Enabled by default at boot") {

    char rx_arg[16] = "";
    sscanf(input_str, "Flash Write Protect: %15s", rx_arg);

    terminalTextAttributesReset();

    bool turnOn;
    if ((strcmp(rx_arg, "On") == 0) || (strcmp(rx_arg, "on") == 0) || (strcmp(rx_arg, "ON") == 0)) {
        turnOn = true;
    } else if ((strcmp(rx_arg, "Off") == 0) || (strcmp(rx_arg, "off") == 0) || (strcmp(rx_arg, "OFF") == 0)) {
        turnOn = false;
    } else {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Argument must be \"On\" or \"Off\" (use \"Flash Write Protect?\" to query the state)\r\n");
        terminalTextAttributesReset();
        return;
    }

    if (turnOn == W25Q128JV_WriteProtectIsEnabled()) {
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("SPI flash write protect is already %s\r\n", turnOn ? "enabled" : "disabled");
        terminalTextAttributesReset();
        return;
    }

    if (turnOn) {
        // Nothing may be stuck in the disk layer's staging buffer once
        // the part starts refusing program/erase
        W25Q128JV_Disk_Sync();
    }

    if (W25Q128JV_WriteProtectSet(turnOn)) {
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("SPI flash write protect %s\r\n",
                turnOn ? "ENABLED -- flash volume is now read-only"
                       : "DISABLED -- flash volume is now writable");
        // An attached host caches the WP state from mount time -- make
        // it re-mount the LUN and re-read MODE SENSE
        USB_MSD_NotifyWriteProtectChanged();
    } else {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("Failed to change SPI flash write protect state\r\n");
    }

    terminalTextAttributesReset();

}

