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
#include "spi/device_driver/w25q128jv.h"
#include "spi/device_driver/w25q128jv_disk.h"
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

// GUI (LVGL on GLCD Layer 1)
#include "gui/gui.h"

// FLIR Lepton 3.5 thermal camera (VoSPI on SPI4, CCI on I2C1)
#include "application/flir/flir.h"
#include "application/flir/flir_vospi.h"
#include "application/flir/flir_process.h"

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
#include "application/image_loader.h"
#include "application/still_capture.h"


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
    
    // ---- Splash-screen fast path ------------------------------------------
    // Everything from here to the backlight enable below is exactly the
    // dependency chain the splash needs, and nothing else: DDR2 (frame
    // buffers + LVGL heap), the SPI flash and its FAT volume (where
    // SPLASH.PNG lives), the GLCD and its Layer 2, the backlight, LVGL
    // (whose heap lodepng decodes into), and the I2C bus master -- the last
    // of these only because lighting the backlight is now conditional on the
    // GT911 touch controller answering, i.e. on the panel being populated.
    // The bus master is register setup only; the one real cost is the GT911
    // probe itself (~70ms of datasheet-mandated reset timing), which is
    // documented at the call site.
    //
    // The RTCC, ADC, I2C *device* probe, SDHC controller and USB device
    // stack all used to run in here; none of them is needed to put a picture
    // on the panel, and together with their console output they were pushing
    // the splash several hundred milliseconds later than necessary. They now
    // run immediately AFTER the splash is lit -- see "deferred bring-up"
    // below. Anything added here in future should be able to justify itself
    // as a splash dependency.

    // Power the FLIR Lepton up BEFORE the I2C bus is touched. This is not
    // optional: with its +2.8V/+1.2V rails down, the unpowered module clamps
    // SDA/SCL low through its I/O structures and every other device on I2C1
    // (temp sensors, power monitors, fuel gauge, touch controller) becomes
    // unreachable. So the sensor is powered for the whole run -- only the
    // video stream is on demand ("FLIR Stream On").
    //
    // This call is the fast part of the sequence (rails -> PGOOD -> master
    // clock -> release power-down -> release reset); the camera's ~950ms boot
    // then overlaps everything below and is collected by FLIR_WaitUntilReady()
    // once I2C and the display are up. Failures latch flir_* error flags.
    reportInit("FLIR Lepton Power", FLIR_PowerOn(), NULL);
    while(usbUartCheckIfBusy());

    // Initialize the 32MB DDR2 SDRAM stacked in this device's package
    reportInit("DDR2 SDRAM Controller", ddr2Initialize(),
            &error_handler.flags.ddr2_init_error);
    while(usbUartCheckIfBusy());

    // Initialize the W25Q128JV SPI NOR flash on SPI3
    reportInit("SPI Flash (W25Q128JV)", W25Q128JV_Initialize(),
            &error_handler.flags.spi_flash_init_error);
    while(usbUartCheckIfBusy());

    // FAT volume "1:" on the SPI flash (512B-sector disk layer over the
    // 4KB-erase part, then mount -- formats on first boot, so the
    // "formatting..." notice is expected exactly once per blank part).
    // SPLASH.PNG is read from this volume, so it is on the fast path.
    reportInit("SPI Flash Filesystem",
            W25Q128JV_Disk_Initialize() && FlashFileIO_MountAndFormatIfNeeded(),
            &error_handler.flags.flash_fs_init_error);
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

    // GLCD Layer 2 (on-demand still-image layer): brought up as early as the
    // hardware allows -- right after GLCD_Initialize(), ahead of the GUI and
    // the Lepton -- so the splash screen below can go up before either of
    // those slower subsystems is ready. Only needs the controller running
    // (glcd.c checks LCDEN itself); it does NOT need GUI_Initialize(), which
    // is why this sits here rather than next to ImageLoader_DisplayPNG().
    reportInit("GLCD Layer 2 (still image)", GLCD_Layer2Initialize(), NULL);
    while(usbUartCheckIfBusy());

    // Backlight brightness is PWM-driven (OC3/Timer4, application/backlight_pwm.c)
    // rather than a plain digital enable pin -- must be initialized after
    // clockInitialize() above (which sets CFGCON.OCACLK=1 under unlock,
    // establishing the OC3-to-Timer4 pairing this driver verifies at init;
    // see backlight_pwm.h for the full story).
    reportInit("Backlight PWM", BacklightPWM_Initialize(),
            &error_handler.flags.backlight_pwm_init_error);
    while(usbUartCheckIfBusy());

    // setup I2C -- still after FLIR_PowerOn() above, which is the constraint
    // that actually matters (an unpowered Lepton clamps SDA/SCL low).
    //
    // This is on the splash fast path only because the backlight is now
    // gated on the GT911 touch controller answering (see the CTP probe
    // below): the bus master has to be up before that probe can run. It is
    // just register setup -- no bus traffic, no measurable time -- so it
    // costs the splash nothing. The 15-device probe it used to be paired
    // with stays deferred, down in "deferred bring-up".
    reportInit("I2C Bus Master", I2C_Initialize(),
            &error_handler.flags.i2c_init_error);
    while(usbUartCheckIfBusy());

    // Bring up LVGL on GLCD Layer 1: a transparent GUI overlay the
    // controller alpha-blends over the Layer 0 image. Must come after
    // GLCD_Initialize() (it enables a layer on the running controller) and
    // after ddr2Initialize() (its two overlay buffers and its 4MB heap are
    // all in DDR2). That heap also backs every PNG decode
    // (application/image_loader.c), so this has to run before the
    // "Display Image:" command can be used.
    reportInit("Graphics User Interface", GUI_Initialize(),
            &error_handler.flags.gui_init_error);
    while(usbUartCheckIfBusy());

    // Display the splash screen now: Layer 2 has been enabled since right
    // after GLCD_Initialize() above, and this is the earliest point the PNG
    // decode itself can run, since it allocates from the LVGL heap that
    // GUI_Initialize() just handed out (application/image_loader.h). Layer 2
    // is fully opaque and painted on top of Layer 0/1, so it hides the (still
    // booting) Lepton and the just-built GUI screens until the splash timer
    // below dismisses it.
    ImageLoader_DisplayPNG(IMAGE_MEDIA_SPI_FLASH, "SPLASH.PNG");

    // Light the panel THE MOMENT the splash is in the frame buffer, and not
    // one line of init sooner or later.
    //
    // BacklightPWM_Initialize() above only programs the OC3/Timer4 PWM; it
    // leaves the duty cycle at 0, i.e. the backlight physically OFF. This
    // SetBrightness() call used to sit after the whole FLIR bring-up and the
    // SD card mount, so the panel stayed dark for that entire stretch even
    // though the splash pixels had been sitting in the Layer 2 buffer the
    // whole time -- and once FLIR_WaitUntilReady() grew a multi-second poll
    // window (application/flir/flir.c), a camera that was slow to answer
    // held the screen black for seconds. THAT was the "splash takes ages to
    // appear" delay; the panel was lit last instead of first.
    //
    // Everything below this line is therefore invisible to the user: the
    // splash is already up and covering Layer 0/1 while it runs.
    //
    // The backlight is gated on the panel's integrated GT911 capacitive
    // touch controller answering on I2C: its ACK is a reliable proxy for
    // "the LCD module is actually populated on this board", which the GLCD
    // Controller itself cannot detect (it only configures MCU-internal
    // registers, so it comes up perfectly happy driving nothing).
    //
    // The probe is the ONE thing allowed to delay the splash: it costs
    // ~70ms, almost all of it the GT911's mandated power-up reset/
    // address-select timing (10ms + 5ms + 50ms of pin toggling, see
    // i2c/device_driver/gt911.h), which cannot be shortened. That buys the
    // panel-presence check; the alternative was gating on the full 15-device
    // probe several hundred milliseconds further down. The rest of that
    // probe stays deferred -- only the CTP is pulled forward here, and
    // I2CDevices_Initialize() below re-probes it harmlessly along with
    // everything else.
    //
    // No error_handler flag is passed to reportInit(): like the "I2C Devices"
    // probe below, I2CDevices_InitializeOne() has already latched this
    // device's OWN flag (I2C_DEV_CTP_1_i2c_error or _config_error, generated
    // from I2C_DEVICE_LIST -- see error_handler.h), which is more specific
    // than any single aggregate flag would be.
    if (reportInit("LCD Touch Controller (GT911)",
            I2CDevices_InitializeOne(I2C_DEV_CTP_1), NULL)) {
        BacklightPWM_SetBrightness(100);
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    LCD Backlight Enabled (touch controller present)\r\n");
    } else {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    LCD Backlight left OFF (touch controller not detected)\r\n");
    }
    terminalTextAttributesReset();
    while(usbUartCheckIfBusy());

    // Non-blocking splash-screen dismiss timer, checked once per superloop
    // pass (see splash_screen_pending below) instead of a blocking
    // softwareDelay() (core/device_control.c -- a raw NOP-counting loop with
    // no time calibration), which used to stall I2C/USB/GUI/FLIR servicing
    // for its whole duration. GUI_GetTickMs() (gui/gui.c) is the same
    // monotonic since-boot millisecond source LVGL's own tick already uses,
    // so no second clock is introduced.
    //
    // The clock starts HERE rather than at the top of boot, so the splash
    // gets its full display time no matter how long the deferred bring-up
    // below takes.
    #define SPLASH_SCREEN_DISPLAY_MS   3000u   // tune to taste
    uint32_t splash_screen_shown_tick_ms = GUI_GetTickMs();
    bool splash_screen_pending = true;

    // ---- Deferred bring-up ------------------------------------------------
    // Everything the splash does NOT depend on, moved below it so none of it
    // sits between power-on and a lit panel. Ordering constraints that still
    // apply within this block: the device probe must precede the battery
    // read; SDHC_Initialize() must precede the card mount further down.
    // (I2C_Initialize() used to head this block; it moved up into the fast
    // path when the backlight became conditional on the CTP probe.)

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

    // setup HLVD
    bool hlvd_ok = hlvdInitialize(5, HLVD_DIRECTION_LOW_VOLTAGE);
    while(!hlvdIsReady());
    reportInit("HLVD", hlvd_ok && hlvdIsReady(),
            &error_handler.flags.hlvd_init_error);
    while(usbUartCheckIfBusy());

    // Bring up the SDHC peripheral itself (clocks/interrupt/register
    // defaults only, no card interaction) -- must succeed regardless of
    // whether a card happens to be inserted. Card detection + mount happens
    // later still, after the FLIR bring-up; see that call site.
    reportInit("SDHC Controller", SDHC_Initialize(),
            &error_handler.flags.sdhc_init_error);
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
    // single aggregate flag is passed here.
    //
    // Note: with no battery installed this reports failure, because the
    // BQ27441 fuel gauge is powered from the cell -- an expected outcome on
    // a bench board running from USB, not a bus fault.
    reportInit("I2C Devices", I2CDevices_Initialize(), NULL);
    while(usbUartCheckIfBusy());

    // FLIR thermal camera, second half. The rails/clock/reset came up before
    // the I2C bring-up above and the camera has been booting ever since; the
    // pieces set up here are the ones that needed DDR2 and the GLCD first:
    //  - the frame-processing palette LUT.
    //  - SPI4 + VoSPI DMA + INT1 registers, left idle until capture starts.
    // FLIR_WaitUntilReady() then collects the boot (normally already elapsed)
    // and runs the CCI RAW14 configuration, leaving the driver READY: powered
    // and configured, but not capturing until FLIR_StreamOn() below.
    FLIRProcess_Initialize();
    FLIR_VOSPI_Initialize();
    if (reportInit("FLIR Lepton Boot + Configuration", FLIR_WaitUntilReady(), NULL)) {
        reportInit("FLIR Video Streaming", FLIR_StreamOn(), NULL);
    } else {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    Thermal video unavailable -- see 'FLIR Status?'\r\n");
        // No thermal video is coming, so put the FLIR error screen up in
        // place of the home screen -- it stays hidden behind the splash
        // (Layer 2) until the dismiss timer below clears it.
        GUI_ShowFlirErrorScreen();
    }
    terminalTextAttributesReset();
    while(usbUartCheckIfBusy());

    // microSD card detection + mount -- deliberately AFTER the FLIR
    // bring-up (and therefore after the splash screen), even though the
    // SDHC controller itself came up much earlier. With a card in the slot
    // at power-on, running the card bring-up in its old spot (right after
    // SDHC_Initialize()) put the slot's power-switch inrush and the whole
    // identification/mount sequence squarely inside the Lepton's ~950ms
    // boot window, and the camera then never reported boot-complete over
    // the CCI -- 100% reproducible with a card present, absent without
    // (bench 2026-08-01; I2C1 itself stayed healthy -- the DS1683 reads
    // below still worked). Keeping every SD-slot event out of the camera's
    // boot+configure window is what this ordering buys. Nothing between
    // here and the old call site needs the card: the USB MSC LUN reports
    // "no media" until the host actually polls (msdLunMediaReady), and
    // enumeration doesn't progress until the superloop anyway.
    //
    // Not wrapped in reportInit()/an error_handler flag: an absent card is
    // normal removable-media behavior, not a controller fault.
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

    // NOTE: the touch-controller-gated backlight enable used to live here.
    // It now runs up in the splash fast path, immediately after the splash
    // is decoded -- gating it down here would have put the panel back to
    // staying dark until the FLIR bring-up finished. See the
    // I2CDevices_InitializeOne(I2C_DEV_CTP_1) call up there.

    // Battery presence heuristic: the BQ27441's BAT_DET flag (Flags()
    // bit3) is forced to 1 on this board -- BIN's NTC + pull-up
    // (TH1401/R1402) is a fixed board component that doesn't disconnect
    // when the 18650 cell is removed from this 2-contact holder -- so it
    // can't distinguish "cell installed" from "cell absent". Use a
    // voltage-threshold heuristic on Voltage() instead: comfortably below
    // any real Li-ion cell's resting voltage (~3.0V+), but a named
    // constant so it's easy to retune.
    #define BATTERY_PRESENT_VOLTAGE_THRESHOLD_V   2.0f
    {
        float battVoltage = 0.0f;
        bool batteryPresent = false;

        if (I2CDevices_IsPresent(I2C_DEV_BATT_1) &&
            I2CDevices_ReadBatteryVoltage(I2C_DEV_BATT_1, &battVoltage)) {
            batteryPresent = (battVoltage >= BATTERY_PRESENT_VOLTAGE_THRESHOLD_V);
        }

        telemetry.battery.present = batteryPresent;

        if (batteryPresent) {
            // Battery installed: let the MAX8903G charge it, and select
            // the higher 500mA USB input current limit.
            BATT_IUSB_PIN = HIGH;    // 500mA USB current limit
            nBATT_CEN_PIN = LOW;     // active-low charge-enable: LOW = enabled
            terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("    Battery detected (%.3f V) -- charging enabled, USB current limit set to 500mA\r\n", battVoltage);
        } else {
            // No battery (or gauge unreachable): leave the boot-default
            // state alone (charging disabled, 100mA limit).
            terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("    No battery detected (%.3f V) -- charging left disabled, USB current limit left at 100mA\r\n", battVoltage);
        }
        terminalTextAttributesReset();
        while(usbUartCheckIfBusy());
    }

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

        // Non-blocking splash-screen dismiss (splash_screen_shown_tick_ms /
        // SPLASH_SCREEN_DISPLAY_MS set above, right after the splash was
        // loaded): hides Layer 2 once it's been up long enough, revealing the
        // thermal video (Layer 0, streaming if the Lepton came up) and GUI
        // (Layer 1). Guarded by splash_screen_pending so it fires exactly
        // once. Unconditional on Lepton/GUI success -- even a failed Lepton
        // boot must not leave the splash stuck on screen forever. Wrap-safe
        // unsigned subtraction, same reasoning as GUI_GetTickMs()'s own
        // comment.
        if (splash_screen_pending &&
                ((GUI_GetTickMs() - splash_screen_shown_tick_ms) >= SPLASH_SCREEN_DISPLAY_MS)) {
            ImageLoader_Clear();
            splash_screen_pending = false;
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

        // report the Power/Shutter button transitions the same Port A ISR
        // latched -- the printing has to happen out here, not at IPL3
        pushbuttonsTasks();

        // a completed SHUTTER press-then-release captures a still: the live
        // thermal video freezes on the last frame, the frame is held in DDR2,
        // and the save-image prompt comes up (application/still_capture.c).
        // Cheap when the FLIR isn't streaming -- Trigger() declines and says
        // why on the console.
        if (shutter_button_capture_request) {
            shutter_button_capture_request = 0;
            StillCapture_Trigger();
        }

        // writes the held frame to the SD card once the prompt is on screen.
        // A state compare when no capture is in flight.
        StillCapture_Tasks();

        // a completed POWER press-then-release is the sleep gesture. This
        // does not return: the board quiesces, executes WAIT, and the next
        // POWER press wakes the core straight into a software reset (see
        // application/power_saving.c). The flag is cleared first anyway so
        // the request can't survive into the next boot through some future
        // early-return path.
        if (power_button_sleep_request) {
            power_button_sleep_request = 0;
            enterLowPowerSleep();
        }

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

        // queue I2C fuel-gauge telemetry read if heartbeatServices() requested it
        if (battery_data_request) {
            updateBatteryTelemetry();
            battery_data_request = 0;
        }

        // time out wedged I2C transfers and restart the queue after a bus error
        I2C_Tasks();

        // fold any finished I2C telemetry reads into the telemetry struct
        telemetryTasks();

        // advance the FLIR boot/config state machine and render any newly
        // captured thermal frame onto Layer 0 (no-op while the sensor is off)
        FLIR_Tasks();

        // redraw the GUI overlay if anything changed, and re-read the values
        // on screen when heartbeatServices() asks (every 500ms)
        GUI_Tasks();

        if (live_telemetry_print_request && live_telemetry_enable) {

            // Redraw the page in place rather than erasing the terminal and
            // re-sending it: terminalLiveScreenBegin()/End() only push out the
            // rows whose text actually changed, so a steady-state refresh
            // costs tens of bytes instead of a couple of kB the port then
            // spends a tenth of a second clocking out
            terminalLiveScreenBegin(live_telemetry_full_repaint != 0);
            live_telemetry_full_repaint = 0;

            terminalRow(TERMINAL_SGR(CYAN_COLOR, BOLD_FONT), "Live system telemetry:");
            terminalBlankRow();

            printCurrentTelemetry();

            terminalRow(TERMINAL_SGR(YELLOW_COLOR, NORMAL_FONT),
                        "Call 'Live Telemetry' command to disable");

            terminalLiveScreenEnd();

            live_telemetry_print_request = 0;

        }
        
        // check to see if a clock fail has occurred and latch it
        clockFailCheck();

        // check to see if the battery charger has flagged a fault and latch it
        batteryFaultCheck();

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

