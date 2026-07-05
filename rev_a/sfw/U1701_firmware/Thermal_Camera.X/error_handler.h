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

// These are macros needed for defining ISRs, included in XC32
#include <sys/attribs.h>

// set this flag high to update the error LEDs the next loop through main()
volatile __attribute__((coherent))  uint8_t update_error_leds_flag;

// Single source of truth for every error handler flag: struct field name
// and the string printed for it. Add/remove a flag by editing only this
// list -- the struct, the name lookup table, and ERROR_HANDLER_NUM_FLAGS
// are all generated from it, so they can't drift out of sync.
#define ERROR_HANDLER_FLAG_LIST(X) \
    X(configuration_error,             "Configuration") \
    X(USB_general_error,               "USB General") \
    X(USB_framing_error,               "USB Framing") \
    X(USB_parity_error,                "USB Parity") \
    X(USB_overrun_error,               "USB Overrun") \
    X(USB_tx_dma_error,                "USB TX DMA") \
    X(USB_rx_dma_error,                "USB RX DMA") \
    X(DMT_error,                       "Deadman Timer") \
    X(system_bus_protection_violation, "System Bus Protection Violation") \
    X(prefetch_module_SEC,             "Prefetch Module SEC") \
    X(clock_failure,                   "Clock Failure") \
    X(WDT_timeout,                     "Watchdog Timer Timeout") \
    X(DMT_timeout,                     "Deadman Timer Timeout") \
    X(mcu_vdd_brownout,                "MCU VDD Brownout") \
    X(ADC_reference_fault,             "ADC Reference Fault") \
    X(ADC_configuration_error,         "ADC Configuration Error")

#define ERROR_HANDLER_FLAG_FIELD(name, string)  uint8_t name;
#define ERROR_HANDLER_FLAG_NAME(name, string)   string,
#define ERROR_HANDLER_FLAG_COUNT(name, string)  +1

#define ERROR_HANDLER_NUM_FLAGS (0 ERROR_HANDLER_FLAG_LIST(ERROR_HANDLER_FLAG_COUNT))

// Error handler structure
// Follow the convention in XC32 user's guide section 8.6.2
// Each flag indicates if the described error has occurred
// This is used for controlling status LEDs and USB debugging
// Access a flag like any C structure
 volatile union error_handler_u {

    struct {

        ERROR_HANDLER_FLAG_LIST(ERROR_HANDLER_FLAG_FIELD)

    } flags;

    uint8_t flag_array[ERROR_HANDLER_NUM_FLAGS];

} error_handler __attribute__((persistent)) __attribute__((coherent));

// this array holds the names of error handler flags, in the same order as
// the flags struct above
const char * error_handler_flag_names[] = {

    ERROR_HANDLER_FLAG_LIST(ERROR_HANDLER_FLAG_NAME)

};


// This function initializes the error handler structure to detect fault conditions
void errorHandlerInitialize(void);

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

// this function checks for clock failures and records them into the error handler
void clockFailCheck(void);

#endif /* _ERROR_HANDLER_H */

/* *****************************************************************************
 End of File
 */