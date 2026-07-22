
#include <xc.h>
#include <stdio.h>
#include <string.h>

#include "application/power_saving.h"
#include "usb_uart/terminal_control.h"
#include "usb_uart/usb_uart.h"

#include "core/device_control.h"
#include "core/32mzda_interrupt_control.h"
#include "core/watchdog_timer.h"
#include "gpio/pin_macros.h"

#include "application/backlight_pwm.h"
#include "i2c/i2c_devices.h"
#include "i2c/i2c_master.h"
#include "sdhc/sd_fileio.h"
#include "sdhc/device_driver/sd_card.h"
#include "spi/flash_fileio.h"
#include "spi/device_driver/sst25vf080b_disk.h"
#include "usb/usb.h"

// This function disables unused peripherals on startup for power savings
// THIS FUNCTION CAN ONLY BE CALLED ONCE DUE TO PMD LOCKOUT AFTER ONE WRITE SESSION
bool PMDInitialize(void) {

    // Unlock PMD
    PMDUnlock();
    /* If a PMD bit is set (1), that peripheral is disabled */
    
    // Enable ADC
    PMD1bits.ADCMD = 0;
 
    // Disable comparator voltage reference
    PMD1bits.CVRMD = 1;

    // Disable charge time measurement unit (unused)
    PMD1bits.CTMUMD = 1;

    // Disable low-voltage detect (unused)
    PMD1bits.LVDMD = 0;

    // Disable both comparators
    PMD2bits.CMP1MD = 1;
    PMD2bits.CMP2MD = 1;
    
    // Disable all input capture modules
    PMD3bits.IC1MD = 1;
    PMD3bits.IC2MD = 1;
    PMD3bits.IC3MD = 1;
    PMD3bits.IC4MD = 1;
    PMD3bits.IC5MD = 1;
    PMD3bits.IC6MD = 1;
    PMD3bits.IC7MD = 1;
    PMD3bits.IC8MD = 1;
    PMD3bits.IC9MD = 1;
    
    // Enable all output compare modules
    PMD3bits.OC1MD = 0;
    PMD3bits.OC2MD = 0;
    PMD3bits.OC3MD = 0;
    PMD3bits.OC4MD = 0;
    PMD3bits.OC5MD = 0;
    PMD3bits.OC6MD = 0;
    PMD3bits.OC7MD = 0;
    PMD3bits.OC8MD = 0;
    PMD3bits.OC9MD = 0;
    
    // Enable all used hardware timers
    PMD4bits.T1MD = 0;
    PMD4bits.T2MD = 0;
    PMD4bits.T3MD = 0;
    PMD4bits.T4MD = 0;
    PMD4bits.T5MD = 0;
    PMD4bits.T6MD = 0;
    PMD4bits.T7MD = 0;
    PMD4bits.T8MD = 0;
    PMD4bits.T9MD = 0;
    
    // enable all UART
    PMD5bits.U1MD = 0;
    PMD5bits.U2MD = 0;
    PMD5bits.U3MD = 0;
    PMD5bits.U4MD = 0;
    PMD5bits.U5MD = 0;
    PMD5bits.U6MD = 0;
    
    // Disable all SPI Modules except SPI3 -- driven by spi3.c for the
    // SST25VF080B SPI NOR flash (sst25vf080b.c); leaving that bit set would
    // make the module inaccessible
    PMD5bits.SPI1MD = 1;
    PMD5bits.SPI2MD = 1;
    PMD5bits.SPI3MD = 0;
    PMD5bits.SPI4MD = 1;
    #ifdef SPI5CON
    PMD5bits.SPI5MD = 1;
    #endif
    #ifdef SPI6CON
    PMD5bits.SPI6MD = 1;
    #endif
    
    // Disable all I2C Modules besides I2C1
    PMD5bits.I2C1MD = 0;
    #ifdef I2C2CON
    PMD5bits.I2C2MD = 1;
    #endif
    PMD5bits.I2C3MD = 1;
    PMD5bits.I2C4MD = 1;
    PMD5bits.I2C5MD = 1;
    
    // USB module stays enabled: it backs the USB mass storage device
    // (usb/usb.c). PMD is one-shot (see this function's header comment),
    // so USB_Initialize() can't undo a disable here -- it checks this bit
    // as a precondition instead.
    PMD5bits.USBMD = 0;

    // Disable CAN modules (unused)
    PMD5bits.CAN1MD = 1;
    PMD5bits.CAN2MD = 1;

    // Enable all reference clocks, per device errata
    PMD6bits.REFO1MD = 0;
    PMD6bits.REFO2MD = 0;
    PMD6bits.REFO3MD = 0;
    PMD6bits.REFO4MD = 0;
    PMD6bits.REFO5MD = 0;
    
    // Disable peripheral master port
    PMD6bits.PMPMD = 1;
    
    // Disable external bus interface (EBI)
    // (During GLCD bring-up this was briefly enabled chasing a Data Bus
    // Error on GLCD SFR access; the real cause turned out to be sub-word
    // register access -- see glcd/glcd.c file header -- and full-word GLCD
    // reads were observed working with EBI/GPU gated off, so both go back
    // to disabled)
    #ifdef EBICS0
    PMD6bits.EBIMD = 1;
    #endif

    // Disable GPU (no register-level driver exists for it -- Microchip
    // documents no hardware interface, Nano-2D/Harmony only)
    PMD6bits.GPUMD = 1;

    // Enable graphics LCD controller -- driven by glcd/glcd.c for the
    // on-board GLT035320240IS1-CTP panel; leaving this bit set would make
    // the controller inaccessible
    PMD6bits.GLCDMD = 0;

    // Enable SD host controller -- driven by sdhc.c for the on-board
    // microSD slot; leaving this bit set would make the controller
    // inaccessible
    PMD6bits.SDHCMD = 0;

    // Disable serial quad interface
    PMD6bits.SQI1MD = 1;
    
    // disable ethernet module
    PMD6bits.ETHMD = 1;
    
    // Enable DMA
    PMD7bits.DMAMD = 0;
    
    // Disable random number generator
    PMD7bits.RNGMD = 1;

    // Enable DDR2 controller -- this device's 32MB DDR2 SDRAM is stacked
    // in-package, driven by ddr2.c; leaving this bit set would make the
    // controller inaccessible
    PMD7bits.DDR2CMD = 0;

    // Lock PMD
    PMDLock();

    // Report success only if the DDR2 controller, SPI3 module, SDHC
    // controller, and GLCD controller were left enabled -- disabling any of
    // these here would freeze all their SFR accesses in
    // ddr2Initialize()/SST25VF080B_Initialize()/SDHC_Initialize()/
    // GLCD_Initialize()
    return (PMD7bits.DDR2CMD == 0) && (PMD5bits.SPI3MD == 0) && (PMD6bits.SDHCMD == 0)
            && (PMD6bits.GLCDMD == 0);

}


// *****************************************************************************
// Section: Low-Power Sleep
// *****************************************************************************
// enterLowPowerSleep() exists so the BQ27441 can take a valid open-circuit
// voltage reading. Impedance Track needs the cell genuinely relaxed to
// update Qmax, and the cell can't simply be unplugged to achieve that --
// the gauge is on the same removable pack contacts and would lose power
// with it. So instead the board makes itself draw as close to nothing as
// possible while staying alive.
//
// Everything below is ordered so that nothing is switched off before the
// thing that depends on it: filesystems are flushed and unmounted before
// their media lose power, and the USB device detaches before its clocks
// stop, so no driver is left mid-transaction when the clocks halt.

// Quiesces the mass-storage stack: both FAT volumes are unmounted and the
// SD card's load switch is opened. The SPI flash has no load switch -- it
// idles in the low-microamp range on its own once deselected -- but its
// disk layer is synced first so no buffered sector is lost.
static void powerDownStorage(void) {

    // USB owns both volumes while attached, so detach before unmounting or
    // the host can issue a transfer into a volume that no longer exists
    USB_Detach();

    Flash_Disk_Sync();
    FlashFileIO_Unmount();

    // Unmount first (flushes FAT state), then drop SD_PWR_EN_PIN. Powering
    // the card down without unmounting risks leaving the FAT dirty.
    SDFileIO_Unmount();
    SD_Card_PowerDown();

}

// Blanks the display path. The panel is put into reset rather than merely
// blanked so its internal drivers stop too, and the backlight -- by far the
// biggest single load on this board -- goes to 0%.
//
// The GLCD controller and its DDR2 framebuffer traffic are deliberately NOT
// touched: both are clocked from SYSCLK/REFCLK, which stop on their own when
// the core enters Sleep, so shutting them down by hand would add risk (the
// GLCD SFR block is 32-bit-access-only, see glcd/glcd.c) for no measurable
// gain.
static void powerDownDisplay(void) {

    BacklightPWM_SetBrightness(0);

    LCD_ENABLE_PIN = LOW;        // panel into reset
    LCD_CTP_RESET_PIN = LOW;     // touch controller into reset

}

// Drops the loads that are just burning current: the indicator LEDs, the
// PGOOD LED bank (which has its own shutdown pin), and the FLIR Lepton's
// power/clock domain along with the two rails that exist only to feed it.
static void powerDownIndicatorsAndSensor(void) {

    HEARTBEAT_LED_PIN = LOW;
    ERROR_LED_PIN = LOW;
    RESET_LED_PIN = LOW;
    CPU_TRAP_LED_PIN = LOW;

    // PGOOD LED bank. This pin drives 74LVC1G97 configurable gates
    // (PGOOD_LEDs.kicad_sch) and nothing else in the firmware touches it, so
    // the asserted polarity is ASSUMED here rather than confirmed against the
    // gate wiring. It is trivially checkable by eye: if the green PGOOD LEDs
    // go out when "Sleep" runs, this is right; if they come ON, flip it.
    PGOOD_LED_SHDN_PIN = LOW;

    // Lepton: assert power-down and reset, stop its master clock, then drop
    // the rails that serve only it (+1.2V and +2.8V)
    nFLIR_PWR_DWN_PIN = LOW;
    nFLIR_RESET_PIN = LOW;
    FLIR_CLK_EN_PIN = LOW;

    POS1P2_RUN_PIN = LOW;
    POS2P8_RUN_PIN = LOW;

}

// Silences every interrupt source that could pull the core straight back out
// of Sleep, leaving exactly one wake source armed: Port A change-notice,
// which carries the POWER button (see application/pushbuttons.c).
//
// The watchdog matters most here. FWDTEN is OFF in configuration, but
// watchdogTimerInitialize() turns it on at run time, and SWDTPS is
// SPS1048576 -- roughly 34 seconds off the ~31 kHz LPRC. Left running it
// would reset the board mid-sleep, every time, which is exactly the
// long-duration rest this mode exists to provide.
static void quiesceWakeSources(void) {

    stopWatchdogTimer();

    // Heartbeat (Timer1) drives all the periodic telemetry requests
    T1CONbits.ON = 0;
    disableInterrupt(timer1);

    // USB is detached by now, but its event sources can still fire
    disableInterrupt(usb_general_event);
    disableInterrupt(usb_dma_event);
    disableInterrupt(usb_suspend_resume_event);

    // I2C is idle by now -- nothing should be queued -- but a stray bus
    // event would wake the core for no reason
    disableInterrupt(i2c1_host_event);
    disableInterrupt(i2c1_bus_collision_event);

    // The one source left armed. Change-notice is asynchronous, so it still
    // works with the peripheral clocks stopped.
    clearInterruptFlag(porta_input_change_interrupt);
    enableInterrupt(porta_input_change_interrupt);

}

void enterLowPowerSleep(void) {

    uint8_t i2cDevicesQuiesced;

    terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("\r\nEntering low-power sleep.\r\n");
    terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    The fuel gauge stays powered so it can take an open-circuit voltage reading.\r\n");
    printf("    Unplug USB/DC power for the cell to actually relax -- a charger keeps it loaded.\r\n");
    printf("    Press RESET to come back.\r\n");
    terminalTextAttributesReset();

    // Everything below kills the UART's clock eventually, so make sure the
    // message above is fully on the wire first
    while(usbUartCheckIfBusy());

    // Stop the heartbeat before anything else. It is what asks for periodic
    // telemetry, so leaving it running would let a fresh batch of I2C reads
    // land in the queue behind the shutdown commands issued below.
    T1CONbits.ON = 0;
    disableInterrupt(timer1);

    // Let anything already queued finish, so no device is left mid-transfer
    // when the bus goes quiet
    while (I2C_IsBusy()) {
        I2C_Tasks();
    }

    powerDownStorage();
    powerDownDisplay();
    powerDownIndicatorsAndSensor();

    // I2C last of the peripherals: the shutdown commands themselves need a
    // working bus, and the fuel gauge is deliberately left alone
    i2cDevicesQuiesced = I2CDevices_EnterLowPower();
    (void)i2cDevicesQuiesced;

    quiesceWakeSources();

    // SLPEN selects Sleep (clocks halted) over Idle (peripheral clocks kept
    // running) for the WAIT instruction below. OSCCON is a system-locked
    // register, hence the unlock/lock.
    deviceUnlock();
    OSCCONbits.SLPEN = 1;
    deviceLock();

    // The WAIT instruction is what actually stops the core clock. The
    // compiler must not hoist anything across it, hence the memory clobber.
    __asm__ volatile ("wait" ::: "memory");

    // Execution resumes here after a Port A change-notice wake. There is no
    // resume path yet -- half the board is powered down and the drivers'
    // in-memory state no longer matches the hardware -- so the only safe
    // thing to do is start over from a known state. This is also what makes
    // the eventual power-button toggle straightforward: a press wakes the
    // core here, and a real implementation would restore the subsystems
    // above in reverse order instead of resetting.
    deviceReset();

}

// This function prints the status of PMD settings
void printPMDStatus(void) {

    terminalTextAttributesReset();    
    
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("Peripheral Module Disable Status:\n\r");
    
    // ADC
    if (PMD1bits.ADCMD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   ADC Enabled:                              %s\n\r", PMD1bits.ADCMD ? "F" : "T");
    
    // CVREF
    if (PMD1bits.CVRMD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   Comparator Voltage Reference Enabled:     %s\n\r", PMD1bits.CVRMD ? "F" : "T");

    // CTMU
    if (PMD1bits.CTMUMD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   Charge Time Measurement Unit Enabled:     %s\n\r", PMD1bits.CTMUMD ? "F" : "T");

    // LVD
    if (PMD1bits.LVDMD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   Low-Voltage Detect Enabled:               %s\n\r", PMD1bits.LVDMD ? "F" : "T");

    // Comparators
    if (PMD2bits.CMP1MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   Comparator 1 Enabled:                     %s\n\r", PMD2bits.CMP1MD ? "F" : "T");
    if (PMD2bits.CMP2MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   Comparator 2 Enabled:                     %s\n\r", PMD2bits.CMP2MD ? "F" : "T");
    
    // Input Capture Modules:
    if (PMD3bits.IC1MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   Input Capture 1 Enabled:                  %s\n\r", PMD3bits.IC1MD ? "F" : "T");
    if (PMD3bits.IC2MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   Input Capture 2 Enabled:                  %s\n\r", PMD3bits.IC2MD ? "F" : "T");
    if (PMD3bits.IC3MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   Input Capture 3 Enabled:                  %s\n\r", PMD3bits.IC3MD ? "F" : "T");
    if (PMD3bits.IC4MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   Input Capture 4 Enabled:                  %s\n\r", PMD3bits.IC4MD ? "F" : "T");
    if (PMD3bits.IC5MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   Input Capture 5 Enabled:                  %s\n\r", PMD3bits.IC5MD ? "F" : "T");
    if (PMD3bits.IC6MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   Input Capture 6 Enabled:                  %s\n\r", PMD3bits.IC6MD ? "F" : "T");
    if (PMD3bits.IC7MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   Input Capture 7 Enabled:                  %s\n\r", PMD3bits.IC7MD ? "F" : "T");
    if (PMD3bits.IC8MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   Input Capture 8 Enabled:                  %s\n\r", PMD3bits.IC8MD ? "F" : "T");
    if (PMD3bits.IC9MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   Input Capture 9 Enabled:                  %s\n\r", PMD3bits.IC9MD ? "F" : "T");
    
    // Output Compare Modules
    if (PMD3bits.OC1MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   Output Compare 1 Enabled:                 %s\n\r", PMD3bits.OC1MD ? "F" : "T");
    if (PMD3bits.OC2MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   Output Compare 2 Enabled:                 %s\n\r", PMD3bits.OC2MD ? "F" : "T");
    if (PMD3bits.OC3MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   Output Compare 3 Enabled:                 %s\n\r", PMD3bits.OC3MD ? "F" : "T");
    if (PMD3bits.OC4MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   Output Compare 4 Enabled:                 %s\n\r", PMD3bits.OC4MD ? "F" : "T");
    if (PMD3bits.OC5MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   Output Compare 5 Enabled:                 %s\n\r", PMD3bits.OC5MD ? "F" : "T");
    if (PMD3bits.OC6MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   Output Compare 6 Enabled:                 %s\n\r", PMD3bits.OC6MD ? "F" : "T");
    if (PMD3bits.OC7MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   Output Compare 7 Enabled:                 %s\n\r", PMD3bits.OC7MD ? "F" : "T");
    if (PMD3bits.OC8MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   Output Compare 8 Enabled:                 %s\n\r", PMD3bits.OC8MD ? "F" : "T");
    if (PMD3bits.OC9MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   Output Compare 9 Enabled:                 %s\n\r", PMD3bits.OC9MD ? "F" : "T");
    
    // Timers
    if (PMD4bits.T1MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   Timer 1 Enabled:                          %s\n\r", PMD4bits.T1MD ? "F" : "T");
    if (PMD4bits.T2MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   Timer 2 Enabled:                          %s\n\r", PMD4bits.T2MD ? "F" : "T");
    if (PMD4bits.T3MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   Timer 3 Enabled:                          %s\n\r", PMD4bits.T3MD ? "F" : "T");
    if (PMD4bits.T4MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   Timer 4 Enabled:                          %s\n\r", PMD4bits.T4MD ? "F" : "T");
    if (PMD4bits.T5MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   Timer 5 Enabled:                          %s\n\r", PMD4bits.T5MD ? "F" : "T");
    if (PMD4bits.T6MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   Timer 6 Enabled:                          %s\n\r", PMD4bits.T6MD ? "F" : "T");
    if (PMD4bits.T7MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   Timer 7 Enabled:                          %s\n\r", PMD4bits.T7MD ? "F" : "T");
    if (PMD4bits.T8MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   Timer 8 Enabled:                          %s\n\r", PMD4bits.T8MD ? "F" : "T");
    if (PMD4bits.T9MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   Timer 9 Enabled:                          %s\n\r", PMD4bits.T9MD ? "F" : "T");
    
    // UART Modules
    if (PMD5bits.U1MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   UART 1 Enabled:                           %s\n\r", PMD5bits.U1MD ? "F" : "T");
    if (PMD5bits.U2MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   UART 2 Enabled:                           %s\n\r", PMD5bits.U2MD ? "F" : "T");
    if (PMD5bits.U3MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   UART 3 Enabled:                           %s\n\r", PMD5bits.U3MD ? "F" : "T");
    if (PMD5bits.U4MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   UART 4 Enabled:                           %s\n\r", PMD5bits.U4MD ? "F" : "T");
    if (PMD5bits.U5MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   UART 5 Enabled:                           %s\n\r", PMD5bits.U5MD ? "F" : "T");
    if (PMD5bits.U6MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   UART 6 Enabled:                           %s\n\r", PMD5bits.U6MD ? "F" : "T");
    
    // SPI Modules
    if (PMD5bits.SPI1MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   SPI 1 Enabled:                            %s\n\r", PMD5bits.SPI1MD ? "F" : "T");
    if (PMD5bits.SPI2MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   SPI 2 Enabled:                            %s\n\r", PMD5bits.SPI2MD ? "F" : "T");
    if (PMD5bits.SPI3MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   SPI 3 Enabled:                            %s\n\r", PMD5bits.SPI3MD ? "F" : "T");
    if (PMD5bits.SPI4MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   SPI 4 Enabled:                            %s\n\r", PMD5bits.SPI4MD ? "F" : "T");
    #ifdef SPI5CON
    if (PMD5bits.SPI5MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   SPI 5 Enabled:                            %s\n\r", PMD5bits.SPI5MD ? "F" : "T");
    #endif
    #ifdef SPI6CON
    if (PMD5bits.SPI6MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   SPI 6 Enabled:                            %s\n\r", PMD5bits.SPI6MD ? "F" : "T");
    #endif
    
    // I2C Modules
    if (PMD5bits.I2C1MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   I2C 1 Enabled:                            %s\n\r", PMD5bits.I2C1MD ? "F" : "T");
    #ifdef I2C2CON
    if (PMD5bits.I2C2MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   I2C 2 Enabled:                            %s\n\r", PMD5bits.I2C2MD ? "F" : "T");
    #endif
    if (PMD5bits.I2C3MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   I2C 3 Enabled:                            %s\n\r", PMD5bits.I2C3MD ? "F" : "T");
    if (PMD5bits.I2C4MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   I2C 4 Enabled:                            %s\n\r", PMD5bits.I2C4MD ? "F" : "T");
    if (PMD5bits.I2C5MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   I2C 5 Enabled:                            %s\n\r", PMD5bits.I2C5MD ? "F" : "T");
    
    // USB Module
    if (PMD5bits.USBMD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   USB Enabled:                              %s\n\r", PMD5bits.USBMD ? "F" : "T");

    // CAN Modules
    if (PMD5bits.CAN1MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   CAN 1 Enabled:                            %s\n\r", PMD5bits.CAN1MD ? "F" : "T");
    if (PMD5bits.CAN2MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   CAN 2 Enabled:                            %s\n\r", PMD5bits.CAN2MD ? "F" : "T");

    // REFCLKS
    if (PMD6bits.REFO1MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   Reference Clock 1 Enabled:                %s\n\r", PMD6bits.REFO1MD ? "F" : "T");
    if (PMD6bits.REFO1MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   Reference Clock 2 Enabled:                %s\n\r", PMD6bits.REFO2MD ? "F" : "T");
    if (PMD6bits.REFO3MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   Reference Clock 3 Enabled:                %s\n\r", PMD6bits.REFO3MD ? "F" : "T");
    if (PMD6bits.REFO4MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   Reference Clock 4 Enabled:                %s\n\r", PMD6bits.REFO4MD ? "F" : "T");
    if (PMD6bits.REFO5MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   Reference Clock 5 Enabled:                %s\n\r", PMD6bits.REFO5MD ? "F" : "T");

    // PMP
    if (PMD6bits.PMPMD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   Parallel Master Port Enabled:             %s\n\r", PMD6bits.PMPMD ? "F" : "T");
    
    // EBI
    #ifdef EBICS0
    if (PMD6bits.EBIMD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   External Bus Interface Enabled:           %s\n\r", PMD6bits.EBIMD ? "F" : "T");
    #endif

    // GPU
    if (PMD6bits.GPUMD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   GPU Enabled:                              %s\n\r", PMD6bits.GPUMD ? "F" : "T");

    // GLCD
    if (PMD6bits.GLCDMD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   Graphics LCD Controller Enabled:          %s\n\r", PMD6bits.GLCDMD ? "F" : "T");

    // SDHC
    if (PMD6bits.SDHCMD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   SD Host Controller Enabled:               %s\n\r", PMD6bits.SDHCMD ? "F" : "T");

    // SQI
    if (PMD6bits.SQI1MD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   Serial Quad Interface Enabled:            %s\n\r", PMD6bits.SQI1MD ? "F" : "T");
    
    // Ethernet
    if (PMD6bits.ETHMD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   Ethernet Enabled:                         %s\n\r", PMD6bits.ETHMD ? "F" : "T");
    
    // DMA
    if (PMD7bits.DMAMD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   Direct Memory Access Enabled:             %s\n\r", PMD7bits.DMAMD ? "F" : "T");
    
    // Random Number Generator
    if (PMD7bits.RNGMD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, REVERSE_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, REVERSE_FONT);
    printf("   Random Number Generator Enabled:          %s\n\r", PMD7bits.RNGMD ? "F" : "T");

    // DDR2 Controller
    if (PMD7bits.DDR2CMD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   DDR2 Controller Enabled:                  %s\n\r", PMD7bits.DDR2CMD ? "F" : "T");

    // PMD Locked?
    if (CFGCONbits.PMDLOCK) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("   PMD Locked:                               %s\n\r", CFGCONbits.PMDLOCK ? "T" : "F");
    
    terminalTextAttributesReset();
    
}

