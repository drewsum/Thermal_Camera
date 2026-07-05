/* ************************************************************************** */
/** Descriptive File Name

  @Company
    Company Name

  @File Name
    filename.h

  @Summary
    Brief description of the file.

  @Description
    Describe the purpose of this file.
 */
/* ************************************************************************** */

#ifndef _USB_UART_RX_LOOKUP_TABLE_H    /* Guard against multiple inclusion */
#define _USB_UART_RX_LOOKUP_TABLE_H

#include <xc.h>
#include <stdio.h>

// Defines a usb_uart serial command function and automatically registers it
// into the usb_uart_commands hash table.
//
// Each invocation generates a small "register" thunk that adds this command to
// the hash table, and drops a pointer to that thunk into the .init_array linker
// section. usbUartInitialize() walks .init_array once at boot and calls every
// thunk, so commands self-register with no hand-maintained list.
//
// Why not __attribute__((constructor))? On this XC32/PIC32 target GCC emits
// constructors into .ctors and the C runtime startup never calls .ctors (nor
// .init_array), so constructors silently never run. We reuse .init_array only as
// storage here: the device linker script already KEEPs it and provides the
// __init_array_start/__init_array_end bounds, so no custom linker script is
// needed, and usbUartInitialize() does the calling explicitly.
//
// Every translation unit using this macro must include usb_uart.h (as before,
// for usbUartAddCommand).
#define USB_UART_COMMAND(func_name, cmd_name, help_msg)                        \
    static void func_name(char *input_str);                                    \
    static void func_name##_register(void) {                                   \
        usbUartAddCommand((cmd_name), (help_msg), func_name);                  \
    }                                                                          \
    static void (* const func_name##_register_ptr)(void)                       \
        __attribute__((used, section(".init_array"))) =                        \
            &func_name##_register;                                             \
    static void func_name(char *input_str)

#endif /* _USB_UART_RX_LOOKUP_TABLE_H */

/* *****************************************************************************
 End of File
 */
