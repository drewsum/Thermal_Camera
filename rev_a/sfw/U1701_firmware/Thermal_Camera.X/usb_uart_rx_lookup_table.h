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
// into the usb_uart_commands hash table via a constructor that runs before
// main(), so no dedicated hash-table initialization function is needed.
#define USB_UART_COMMAND(func_name, cmd_name, help_msg)                       \
    static void func_name(char *input_str);                                   \
    static void __attribute__((constructor)) func_name##_autoregister(void) { \
        usbUartAddCommand(cmd_name, help_msg, func_name);                     \
    }                                                                         \
    static void func_name(char *input_str)

#endif /* _USB_UART_RX_LOOKUP_TABLE_H */

/* *****************************************************************************
 End of File
 */
