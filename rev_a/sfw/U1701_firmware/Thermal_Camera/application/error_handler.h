/* ************************************************************************** */
/** Error Handler

  @Company
 Marquette Senior Design E44 2018-2019

  @File Name
    error_handler.h

  @Summary
 Provides functions and variables/structures for recording and reporting errors
 during runtime

 */
/* ************************************************************************** */

#ifndef _ERROR_HANDLER_H    /* Guard against multiple inclusion */
#define _ERROR_HANDLER_H

#include <xc.h>
#include <stdbool.h>

// These are macros needed for defining ISRs, included in XC32
#include <sys/attribs.h>

// pulls in I2C_DEVICE_LIST, from which the per-I2C-device error flags below
// are generated
#include "i2c/i2c_devices.h"

// set this flag high to update the error LEDs the next loop through main()
volatile __attribute__((coherent))  uint8_t update_error_leds_flag;

// Single source of truth for every error handler flag: struct field name
// and the string printed for it. Add/remove a flag by editing only this
// list -- the struct, the name lookup table, and ERROR_HANDLER_NUM_FLAGS
// are all generated from it, so they can't drift out of sync.
#define ERROR_HANDLER_FLAG_LIST(X) \
    X(configuration_error,             "Configuration") \
    X(USB_general_error,               "USB UART General") \
    X(USB_framing_error,               "USB UART Framing") \
    X(USB_parity_error,                "USB UART Parity") \
    X(USB_overrun_error,               "USB UART Overrun") \
    X(USB_tx_dma_error,                "USB UART TX DMA") \
    X(USB_rx_dma_error,                "USB UART RX DMA") \
    X(DMT_error,                       "Deadman Timer") \
    X(system_bus_protection_violation, "System Bus Protection Violation") \
    X(prefetch_module_SEC,             "Prefetch Module SEC") \
    X(clock_failure,                   "Clock Failure") \
    X(WDT_timeout,                     "Watchdog Timer Timeout") \
    X(DMT_timeout,                     "Deadman Timer Timeout") \
    X(mcu_vdd_brownout,                "MCU VDD Brownout") \
    X(mcu_vdd_hlvd_brownout,           "MCU VDD HLVD Brownout") \
    X(mcu_vdd_core_hlvd_brownout,      "MCU VDD Core HLVD Brownout") \
    X(ADC_reference_fault,             "ADC Reference Fault") \
    X(ADC_configuration_error,         "ADC Configuration Error") \
    X(DDR2_mpll_vreg_timeout,          "DDR2 MPLL Voltage Regulator Timeout") \
    X(DDR2_mpll_lock_timeout,          "DDR2 MPLL Lock Timeout") \
    X(DDR2_init_sequence_timeout,      "DDR2 Init Command Sequence Timeout") \
    X(DDR2_calibration_timeout,        "DDR2 Self-Calibration Timeout") \
    X(DDR2_self_test_failed,           "DDR2 Boot Self-Test Failed") \
    X(clock_init_error,                "System Clock Init") \
    X(heartbeat_timer_init_error,      "Heartbeat Timer Init") \
    X(usb_uart_init_error,             "USB UART Init") \
    X(prefetch_init_error,             "Prefetch Init") \
    X(pmd_init_error,                  "Peripheral Module Disable Init") \
    X(watchdog_init_error,             "Watchdog Timer Init") \
    X(rtcc_init_error,                 "RTCC Init") \
    X(adc_init_error,                  "ADC Init") \
    X(i2c_init_error,                  "I2C Master Init") \
    X(hlvd_init_error,                 "HLVD Init") \
    X(ddr2_init_error,                 "DDR2 Init") \
    X(spi_flash_init_error,            "SPI Flash Init") \
    X(sdhc_init_error,                 "SDHC Init") \
    X(flash_fs_init_error,             "SPI Flash Filesystem Init") \
    X(usb_msd_init_error,              "USB Mass Storage Init") \
    X(glcd_init_error,                 "GLCD Init") \
    X(backlight_pwm_init_error,        "Backlight PWM Init") \
    X(battery_charger_fault,           "Battery Charger Fault (MAX8903 nFLT)") \
    X(gui_init_error,                  "GUI (LVGL) Init") \
    X(gui_lvgl_error,                  "LVGL Runtime Error") \
    X(gui_heap_exhausted,              "LVGL Heap Exhausted") \
    X(gui_vsync_timeout,               "GLCD Overlay VSync Timeout")

#define ERROR_HANDLER_FLAG_FIELD(name, string)  uint8_t name;
#define ERROR_HANDLER_FLAG_NAME(name, string)   string,
#define ERROR_HANDLER_FLAG_COUNT(name, string)  +1

// On top of the base list above, one I2C-error flag is generated per
// physical I2C device in I2C_DEVICE_LIST (i2c_devices.h), so that list stays
// the single source of truth: adding a device automatically adds its flag to
// the struct, the name table, the status print, and the error LED check.
// Field names are the device enum name + _i2c_error (e.g.
// error_handler.flags.I2C_DEV_TEMP_1_i2c_error). Unlike the "_init_error"
// flags in the base list above (which only ever fire once, during that
// subsystem's one-time boot init), a device's flag here covers its entire
// operating lifetime: I2CDevices_ReportI2CError() (i2c_devices.c) sets it
// when the device fails to verify during I2CDevices_Initialize() AND when
// it later fails to ACK or return valid data on any runtime read (NACK,
// bus timeout, etc). Like every other error_handler flag it only latches
// on failure -- a later successful read does not clear it, and it stays
// set until clearErrorHandler() runs.
#define I2C_DEVICE_ERROR_FLAG_FIELD(name, kind, address, label, refdes)  uint8_t name##_i2c_error;
#define I2C_DEVICE_ERROR_FLAG_NAME(name, kind, address, label, refdes)   label " (" refdes ") I2C",
#define I2C_DEVICE_ERROR_FLAG_COUNT(name, kind, address, label, refdes)  +1

// A second per-device flag, generated the same way, for the kind-specific
// setup step I2CDevices_ConfigureOne() runs after a device verifies (e.g.
// INA231A_Configure(), BQ27441_ConfigureOpConfig()). This is deliberately
// separate from the _i2c_error flag above: a device that ACKs, identifies,
// and reads back fine but whose configuration sequence fails is a very
// different fault from one that isn't talking on the bus at all, and
// folding both into one flag makes a working device look unreachable.
// Field names are the device enum name + _config_error.
#define I2C_DEVICE_CONFIG_FLAG_FIELD(name, kind, address, label, refdes)  uint8_t name##_config_error;
#define I2C_DEVICE_CONFIG_FLAG_NAME(name, kind, address, label, refdes)   label " (" refdes ") Configuration",
#define I2C_DEVICE_CONFIG_FLAG_COUNT(name, kind, address, label, refdes)  +1

#define ERROR_HANDLER_NUM_BASE_FLAGS (0 ERROR_HANDLER_FLAG_LIST(ERROR_HANDLER_FLAG_COUNT))
#define ERROR_HANDLER_NUM_FLAGS (ERROR_HANDLER_NUM_BASE_FLAGS \
                                 I2C_DEVICE_LIST(I2C_DEVICE_ERROR_FLAG_COUNT) \
                                 I2C_DEVICE_LIST(I2C_DEVICE_CONFIG_FLAG_COUNT))

// Accesses the I2C-error and configuration-error flags for I2C device `id`
// (an I2C_DEVICE_ID) by index: the per-device flags sit directly after the
// base flags in flag_array, all I2C_DEVICE_COUNT error flags first and then
// all I2C_DEVICE_COUNT configuration flags, each in I2C_DEVICE_LIST order.
// Prefer calling I2CDevices_ReportI2CError()/I2CDevices_ReportConfigError()
// (i2c_devices.h) over writing these directly.
#define ERROR_HANDLER_I2C_DEVICE_FLAG(id) (error_handler.flag_array[ERROR_HANDLER_NUM_BASE_FLAGS + (id)])
#define ERROR_HANDLER_I2C_CONFIG_FLAG(id) (error_handler.flag_array[ERROR_HANDLER_NUM_BASE_FLAGS + I2C_DEVICE_COUNT + (id)])

// Error handler structure
// Follow the convention in XC32 user's guide section 8.6.2
// Each flag indicates if the described error has occurred
// This is used for controlling status LEDs and USB debugging
// Access a flag like any C structure
 volatile union error_handler_u {

    struct {

        ERROR_HANDLER_FLAG_LIST(ERROR_HANDLER_FLAG_FIELD)
        I2C_DEVICE_LIST(I2C_DEVICE_ERROR_FLAG_FIELD)
        I2C_DEVICE_LIST(I2C_DEVICE_CONFIG_FLAG_FIELD)

    } flags;

    uint8_t flag_array[ERROR_HANDLER_NUM_FLAGS];

} error_handler __attribute__((persistent)) __attribute__((coherent));

// this array holds the names of error handler flags, in the same order as
// the flags struct above
const char * error_handler_flag_names[] = {

    ERROR_HANDLER_FLAG_LIST(ERROR_HANDLER_FLAG_NAME)
    I2C_DEVICE_LIST(I2C_DEVICE_ERROR_FLAG_NAME)
    I2C_DEVICE_LIST(I2C_DEVICE_CONFIG_FLAG_NAME)

};


// This function initializes the error handler structure to detect fault conditions
bool errorHandlerInitialize(void);

// System Bus Protection Violation interrupt service routine
void __ISR(_SYSTEM_BUS_PROTECTION_VECTOR, ipl1SRS) systemBusProtectionISR(void);

// This function is called when a general exception occurs
void __attribute__((nomips16)) _general_exception_handler(void);

// This function is called when a TRB exception occurs
void __attribute__((nomips16)) _simple_tlb_refill_exception_handler(void);

// This function is called when a cache error occurs
void __attribute__((nomips16)) _cache_err_exception_handler(void);

// This function is called when a bootstrap exception occurs
void __attribute__((nomips16)) _bootstrap_exception_handler(void);

// This function prints the status of the error handler flags
void printErrorHandlerStatus(void);

// This function clears the error handler flags
void clearErrorHandler(void);

// This function updates the error LEDs based on the error handler state
void updateErrorLEDs(void);

// This function prints short strings during a CPU exception
void exceptionPrint(char *input_string);

// This function prints a label followed by a 32-bit hex value during a CPU exception
void exceptionPrintHex(char *label, uint32_t value);

// this function checks for clock failures and records them into the error handler
void clockFailCheck(void);

// this function checks for a MAX8903 battery charger fault and records it into the error handler
void batteryFaultCheck(void);

#endif /* _ERROR_HANDLER_H */

/* *****************************************************************************
 End of File
 */