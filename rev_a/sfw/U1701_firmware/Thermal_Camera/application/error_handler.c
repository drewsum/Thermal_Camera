
#include <xc.h>
#include <stdio.h>
#include <string.h>

// These are macros needed for defining ISRs, included in XC32
#include <sys/attribs.h>

#include "application/error_handler.h"
#include "core/32mzda_interrupt_control.h"
#include "gpio/pin_macros.h"
#include "usb_uart/terminal_control.h"
#include "usb_uart/usb_uart.h"
#include "core/watchdog_timer.h"

// This function initializes the error handler structure to detect fault conditions
bool errorHandlerInitialize(void) {
 
    // Setup system bus protection violation interrupt
    disableInterrupt(system_bus_protection_violation);
    setInterruptPriority(system_bus_protection_violation, 1);
    setInterruptSubpriority(system_bus_protection_violation, 1);
    clearInterruptFlag(system_bus_protection_violation);
    enableInterrupt(system_bus_protection_violation);

    // No software-detectable failure mode for error handler setup
    return true;

}

// System Bus Protection Violation interrupt service routine
void __ISR(_SYSTEM_BUS_PROTECTION_VECTOR, ipl1SRS) systemBusProtectionISR(void) {
 
    // Record a system bus protection violation occurred
    error_handler.flags.system_bus_protection_violation = 1;
    clearInterruptFlag(system_bus_protection_violation);
    
}

// This function is called when a general exception occurs
void __attribute__((nomips16)) _general_exception_handler(void) {
    
    // Signal to user something really bad happened
    CPU_TRAP_LED_PIN = HIGH;
    
    // Disable global interrupts
    setGlobalInterruptsState(0);
    
    // Clear watchdog and deadman to give user time to see error state
    kickTheDog();
    holdThumbTighter();
    
    exceptionPrint(" \033[0;31;40mCPU General Exception! EXCCODE: ");
    
    uint8_t exception_code = (_CP0_GET_CAUSE() >> 2) & 0b11111;
    char exception_code_number = exception_code + 48;
    USB_UART_TX_REG = exception_code_number;
    exceptionPrint("\n\r");

    exceptionPrintHex("EPC: 0x", _CP0_GET_EPC());
    exceptionPrintHex("BadVAddr: 0x", _CP0_GET_BADVADDR());
    exceptionPrintHex("Cause: 0x", _CP0_GET_CAUSE());

    // On a Data Bus Error (EXCCODE 7), also dump the System Bus per-target
    // error flags and the target-12 (GLCD/GPU/DDR2PHY/DDR2SFR) error log:
    // the interconnect latches what it rejected -- SBT12ELOG1 holds the
    // command (CMD<2:0>), region, initiator ID, and error code fields,
    // decoded against the device datasheet's System Bus register section.
    // Added during GLCD bring-up (2026-07-19, GLCD SFR writes bus-faulting),
    // useful for any future DBE too.
    if (exception_code == 7) {
        exceptionPrintHex("SBFLAG0: 0x", SBFLAG0);
        exceptionPrintHex("SBFLAG1: 0x", SBFLAG1);
        exceptionPrintHex("SBFLAG2: 0x", SBFLAG2);
        exceptionPrintHex("SBFLAG3: 0x", SBFLAG3);
        exceptionPrintHex("SBT12ELOG1: 0x", SBT12ELOG1);
        exceptionPrintHex("SBT12ELOG2: 0x", SBT12ELOG2);
        exceptionPrintHex("SBT12ECON: 0x", SBT12ECON);
        exceptionPrintHex("SBT12REG0: 0x", SBT12REG0);
        exceptionPrintHex("SBT12RD0: 0x", SBT12RD0);
        exceptionPrintHex("SBT12WR0: 0x", SBT12WR0);
    }

    // Give up
    // Wait for watchdog to save us
    while(1);

}

// This function is called when a TLB exception occurs
void __attribute__((nomips16)) _simple_tlb_refill_exception_handler(void) {

    // Signal to user something really bad happened
    CPU_TRAP_LED_PIN = HIGH;
    
    // Clear watchdog to give user time to see error state
    kickTheDog();
    holdThumbTighter();
    
    exceptionPrint("\033[0;31;40mCPU TLB Refill Exception!\n\r");

    exceptionPrintHex("EPC: 0x", _CP0_GET_EPC());
    exceptionPrintHex("BadVAddr: 0x", _CP0_GET_BADVADDR());
    exceptionPrintHex("Cause: 0x", _CP0_GET_CAUSE());

    // Give up
    // Wait for watchdog to save us
    while(1);
    
}

// This function is called when a cache error occurs
void __attribute__((nomips16)) _cache_err_exception_handler(void) {

// Signal to user something really bad happened
    CPU_TRAP_LED_PIN = HIGH;
    
    // Clear watchdog to give user time to see error state
    kickTheDog();
    holdThumbTighter();
    
    exceptionPrint("\033[0;31;40mCPU Cache Exception!\n\r");

    exceptionPrintHex("EPC: 0x", _CP0_GET_EPC());
    exceptionPrintHex("BadVAddr: 0x", _CP0_GET_BADVADDR());
    exceptionPrintHex("Cause: 0x", _CP0_GET_CAUSE());

    // Give up
    // Wait for watchdog to save us
    while(1);

}

// This function is called when a bootstrap exception occurs
void __attribute__((nomips16)) _bootstrap_exception_handler(void) {

// Signal to user something really bad happened
    CPU_TRAP_LED_PIN = HIGH;
    
    // Clear watchdog to give user time to see error state
    kickTheDog();
    holdThumbTighter();
    
    exceptionPrint("\033[0;31;40mCPU Bootstrap Exception!\n\r");

    exceptionPrintHex("EPC: 0x", _CP0_GET_EPC());
    exceptionPrintHex("BadVAddr: 0x", _CP0_GET_BADVADDR());
    exceptionPrintHex("Cause: 0x", _CP0_GET_CAUSE());

    // Give up
    // Wait for watchdog to save us
    while(1);
    
}

// Expands to one print statement per flag in ERROR_HANDLER_FLAG_LIST,
// referencing the named struct field directly instead of indexing flag_array
#define ERROR_HANDLER_FLAG_PRINT(name, string) \
    if (error_handler.flags.name) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT); \
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT); \
    printf("    %s Error %s\n\r", string, error_handler.flags.name ? "has occurred" : "has not occurred");

// Same as ERROR_HANDLER_FLAG_PRINT, but for the per-I2C-device error flags
// generated from I2C_DEVICE_LIST (see error_handler.h)
#define I2C_DEVICE_ERROR_FLAG_PRINT(name, kind, address, label, refdes) \
    if (error_handler.flags.name##_i2c_error) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT); \
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT); \
    printf("    %s Error %s\n\r", label " (" refdes ") I2C", error_handler.flags.name##_i2c_error ? "has occurred" : "has not occurred");

// This function prints the status of the error handler flags
void printErrorHandlerStatus(void) {

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);

    // Print heading
    printf("Error Handler Status:\n\r");

    ERROR_HANDLER_FLAG_LIST(ERROR_HANDLER_FLAG_PRINT)
    I2C_DEVICE_LIST(I2C_DEVICE_ERROR_FLAG_PRINT)

    terminalTextAttributesReset();

}

// This function clears the error handler flags
void clearErrorHandler(void) {
 
    // loop through all error handler flags and clear
    uint32_t index;
    for (index = 0; index < ERROR_HANDLER_NUM_FLAGS; index++) {
     
        error_handler.flag_array[index] = 0;
        
    }
    
}

// Expands to one check per flag in ERROR_HANDLER_FLAG_LIST, referencing the
// named struct field directly instead of indexing flag_array
#define ERROR_HANDLER_FLAG_CHECK_LED(name, string) \
    if (error_handler.flags.name) ERROR_LED_PIN = HIGH;

// Same as ERROR_HANDLER_FLAG_CHECK_LED, but for the per-I2C-device error
// flags generated from I2C_DEVICE_LIST (see error_handler.h)
#define I2C_DEVICE_ERROR_FLAG_CHECK_LED(name, kind, address, label, refdes) \
    if (error_handler.flags.name##_i2c_error) ERROR_LED_PIN = HIGH;

// This function updates the error LEDs based on the error handler state
void updateErrorLEDs(void) {

    // Clear error LED for now since we'll set it below if we need to
    ERROR_LED_PIN = LOW;

    ERROR_HANDLER_FLAG_LIST(ERROR_HANDLER_FLAG_CHECK_LED)
    I2C_DEVICE_LIST(I2C_DEVICE_ERROR_FLAG_CHECK_LED)

    update_error_leds_flag = 0;

}

// This function prints a label followed by a 32-bit value in hex during a
// CPU exception, using the same raw register access as exceptionPrint()
void exceptionPrintHex(char *label, uint32_t value) {

    exceptionPrint(label);

    char hex_chars[] = "0123456789ABCDEF";
    int nibble;
    for (nibble = 7; nibble >= 0; nibble--) {

        USB_UART_TX_REG = hex_chars[(value >> (nibble * 4)) & 0xF];
        while(USB_UART_TX_STA_BITFIELD.UTXBF);

    }

    exceptionPrint("\n\r");

}

// This function prints short strings during a CPU exception
void exceptionPrint(char *input_string) {
 
    // loop through all input characters
    int i;
    for (i = 0; i < strlen(input_string); i++) {
     
        // if we're done with the string, return
        if (input_string[i] == '\0') return;
        
        // send single character
        USB_UART_TX_REG = input_string[i];
        
        // wait for buffer to open
        while(USB_UART_TX_STA_BITFIELD.UTXBF);
        
        
    }
    
}
// this function checks for clock failures and records them into the error handler
void clockFailCheck(void) {

    if (OSCCONbits.CF) error_handler.flags.clock_failure = 1;
    
}