/* ************************************************************************** */
/** Descriptive File Name

  @Company
 Drew Maatman

  @File Name
    pin_macros.h

  @Summary
    Macros for setting and checking the state of GPIO pins

  @Description
    Macros for setting and checking the state of GPIO pins
 */
/* ************************************************************************** */

#ifndef _PIN_MACROS_H    /* Guard against multiple inclusion */
#define _PIN_MACROS_H

#include <xc.h>

#define HIGH    1
#define LOW     0

// These pin macros allow for easier manipulation of GPIO with matching
// signal names from the hardware schematic

// Port A
#define POS3P3_USB_PGOOD_PIN        PORTAbits.RA2
#define CAP_TOUCH_SHUTTER_PIN       PORTAbits.RA9
#define CAP_TOUCH_POWER_PIN         PORTAbits.RA10

// Port B
#define nFLASH_SPI_CS_PIN           LATBbits.LATB4
#define POS2P8_PGOOD_PIN            PORTBbits.RB8
#define FLIR_CLK_EN_PIN             LATBbits.LATB11
#define nFLASH_SPI_WP_PIN           LATBbits.LATB12
#define nFLIR_PWR_DWN_PIN           LATBbits.LATB13
#define POS2P8_RUN_PIN              LATBbits.LATB15

// Port C
#define POS12_PGOOD_PIN             PORTCbits.RC1
#define nBATT_CHG_PIN               PORTCbits.RC2
#define nBATT_DOK_PIN               PORTCbits.RC3
#define HEARTBEAT_LED_PIN           LATCbits.LATC4

// Port D
#define FLIR_VOSPI_VSYNC_PIN        PORTDbits.RD0
#define nFLIR_VOSPI_CS_PIN          LATDbits.LATD1
#define nFLIR_RESET_PIN             LATDbits.LATD15

// Port E
#define nBATT_FLT_PIN               PORTEbits.RE0
#define nBATT_UOK_PIN               PORTEbits.RE1
#define RESET_LED_PIN               LATEbits.LATE4
#define nBATT_CEN_PIN               LATEbits.LATE6
#define BATT_IUSB_PIN               LATEbits.LATE7
#define BATT_LOWBATT_PIN            PORTEbits.RE8
#define PGOOD_LED_SHDN_PIN          LATEbits.LATE9

// Port F
#define CPU_TRAP_LED_PIN            LATFbits.LATF2
#define POS1P2_RUN_PIN              LATEFbits.LATF12

// Port G
#define nSD_SPI_CS_PIN              LATGbits.LATG9
#define SD_PWR_EN_PIN               LATGbits.LATG15

// Port H
#define POS3P0_PGOOD_PIN            PORTHbits.RH3
#define POS1P2_PGOOD_PIN            PORTHbits.RH4
#define POS1P8_PGOOD_PIN            PORTHbits.RH7
#define ERROR_LED_PIN               LATHbits.LATH14

// Port J
#define LCD_CTP_INT_PIN             PORTJbits.RJ8
#define LCD_CTP_RESET_PIN           LATJbits.LATJ9
#define LCD_ENABLE_PIN              LATJbits.LATJ11

// Port K - No GPIO pins used

#endif /* _PIN_MACROS_H */

/* *****************************************************************************
 End of File
 */
