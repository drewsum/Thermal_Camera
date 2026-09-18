
#include <xc.h>
#include <stdio.h>
#include <string.h>

#include "gpio/pic32mzda_gpio_setup.h"
#include "usb_uart/terminal_control.h"

// initializes port A GPIO pins
void portAGPIOInitialize (void) {
    
    gpioPinSetup(gpio_port_a, 0, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_a, 1, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_a, 2, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_a, 3, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_a, 4, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_a, 5, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_a, 6, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_a, 7, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_a, 9, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_a, 10, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_a, 14, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_a, 15, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    
}

// initializes port B GPIO pins
void portBGPIOInitialize (void) {

    gpioPinSetup(gpio_port_b, 0, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_b, 1, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_b, 2, TRIS_OUTPUT, LAT_HIGH, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_b, 3, TRIS_OUTPUT, LAT_HIGH, ODC_DISABLE, ANALOG_DISABLE);
    RPB3Rbits.RPB3R = U5TX_PPS_OUTPUT;                                                  // Assign RPB3 as U5TX
    gpioPinSetup(gpio_port_b, 4, TRIS_OUTPUT, LAT_HIGH, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_b, 5, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    RPB5Rbits.RPB5R = OC3_PPS_OUTPUT;                                                   // Assign RPB5 as OC3
    gpioPinSetup(gpio_port_b, 6, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    U4RXRbits.U4RXR = RPB6_PPS_INPUT;                                                   // Assign RPB6 as U4RX (USB UART receiver)
    gpioPinSetup(gpio_port_b, 7, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_b, 8, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_b, 9, TRIS_OUTPUT, LAT_HIGH, ODC_DISABLE, ANALOG_DISABLE);
    RPB9Rbits.RPB9R = SDO3_PPS_OUTPUT;                                                  // Assign RPB9 as SDO3
    gpioPinSetup(gpio_port_b, 10, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    SDI3Rbits.SDI3R = RPB10_PPS_INPUT;                                                  // Assign RPB10 as SDI3
    gpioPinSetup(gpio_port_b, 11, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_b, 12, TRIS_OUTPUT, LAT_HIGH, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_b, 13, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_b, 14, TRIS_OUTPUT, LAT_HIGH, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_b, 15, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);

}

// initializes port C GPIO pins
void portCGPIOInitialize (void) {

    gpioPinSetup(gpio_port_c, 1, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_c, 2, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_c, 3, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_c, 4, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    RPC4Rbits.RPC4R = OC4_PPS_OUTPUT;                                                   // Assign RPC4 as OC4
    gpioPinSetup(gpio_port_c, 12, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_c, 13, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_c, 14, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_c, 15, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    
}

// initializes port D GPIO pins
void portDGPIOInitialize (void) {
    
    gpioPinSetup(gpio_port_d, 0, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    INT1Rbits.INT1R = RPD0_PPS_INPUT;                                                   // Assign RPD0 as INT1
    gpioPinSetup(gpio_port_d, 1, TRIS_OUTPUT, LAT_HIGH, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_d, 2, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_d, 3, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_d, 4, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_d, 5, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    RPD5Rbits.RPD5R = SDO4_PPS_OUTPUT;                                                  // Assign RPD5 as SDO4
    gpioPinSetup(gpio_port_d, 6, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_d, 7, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    SDI4Rbits.SDI4R = RPD7_PPS_INPUT;                                                   // Assign RPD7 as SDI4
    gpioPinSetup(gpio_port_d, 9, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_d, 10, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_d, 11, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    RPD11Rbits.RPD11R = REFCLKO1_PPS_OUTPUT;                                            // Assign RPD11 to REFCLKO1
    gpioPinSetup(gpio_port_d, 12, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_d, 13, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_d, 14, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_d, 15, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);

}

// initializes port E GPIO pins
void portEGPIOInitialize (void) {

        gpioPinSetup(gpio_port_e, 0, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
        gpioPinSetup(gpio_port_e, 1, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
        gpioPinSetup(gpio_port_e, 2, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
        gpioPinSetup(gpio_port_e, 3, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
        gpioPinSetup(gpio_port_e, 4, TRIS_OUTPUT, LAT_HIGH, ODC_DISABLE, ANALOG_DISABLE);
        gpioPinSetup(gpio_port_e, 5, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
        gpioPinSetup(gpio_port_e, 6, TRIS_OUTPUT, LAT_HIGH, ODC_DISABLE, ANALOG_DISABLE);
        gpioPinSetup(gpio_port_e, 7, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
        gpioPinSetup(gpio_port_e, 8, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
        gpioPinSetup(gpio_port_e, 9, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    
}

// initializes port F GPIO pins
void portFGPIOInitialize (void) {

        gpioPinSetup(gpio_port_f, 0, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
        gpioPinSetup(gpio_port_f, 1, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
        gpioPinSetup(gpio_port_f, 2, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
        gpioPinSetup(gpio_port_f, 3, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
        gpioPinSetup(gpio_port_f, 4, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
        gpioPinSetup(gpio_port_f, 5, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
        gpioPinSetup(gpio_port_f, 8, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
        gpioPinSetup(gpio_port_f, 12, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
        gpioPinSetup(gpio_port_f, 13, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
        
}

// initializes port G GPIO pins
void portGGPIOInitialize (void) {

    gpioPinSetup(gpio_port_g, 0, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_g, 1, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_g, 6, TRIS_OUTPUT, LAT_HIGH, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_g, 7, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    SDI2Rbits.SDI2R = RPG7_PPS_INPUT;                                                       // Assign RPG7 as SDI2
    gpioPinSetup(gpio_port_g, 8, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    RPG8Rbits.RPG8R = SDO2_PPS_OUTPUT;                                                      // Assign RPG8 as SDO2
    gpioPinSetup(gpio_port_g, 9, TRIS_OUTPUT, LAT_HIGH, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_g, 12, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_g, 13, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_g, 14, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_g, 15, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    
}

// initializes port H GPIO pins
void portHGPIOInitialize (void) {

    gpioPinSetup(gpio_port_h, 0, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_h, 1, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_h, 2, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_h, 3, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_h, 4, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_h, 5, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_h, 6, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_h, 7, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_h, 8, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_h, 9, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_h, 10, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_h, 11, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_h, 12, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_h, 13, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_h, 14, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_h, 15, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    
}

// initializes port J GPIO pins
void portJGPIOInitialize (void) {
    
    gpioPinSetup(gpio_port_j, 0, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_j, 1, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_j, 2, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_j, 3, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_j, 4, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_j, 5, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_j, 6, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_j, 7, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_j, 8, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_j, 9, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_j, 10, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_j, 11, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_j, 12, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_j, 13, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_j, 14, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_j, 15, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);

}

// initializes port K GPIO pins
void portKGPIOInitialize (void) {
    
    gpioPinSetup(gpio_port_k, 0, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_k, 1, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_k, 2, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_k, 3, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_k, 4, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_k, 5, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_k, 6, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
    gpioPinSetup(gpio_port_k, 7, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);

}


// don't change these
// this function allows for a more convenient way to setup pins
void gpioPinSetup(port_name_t port_name, 
    uint8_t pin_number,
    uint8_t tris_setting,
    uint8_t lat_setting,
    uint8_t open_drain_setting,
    uint8_t analog_setting) {

    #ifdef LATA
    if (port_name == gpio_port_a) {
        // set LAT for this pin
        LATACLR = (1 << pin_number);
        LATASET = (lat_setting << pin_number);
        // set ODC for this pin
        ODCACLR = (1 << pin_number);
        ODCASET = (open_drain_setting << pin_number);
        // set ANSEL for this pin
        #ifdef ANSELA
        ANSELACLR = (1 << pin_number);
        ANSELASET = (analog_setting << pin_number);
        #endif
        // Set TRIS for this pin
        TRISACLR = (1 << pin_number);
        TRISASET = (tris_setting << pin_number);
    }
    #endif

    #ifdef LATB
    if (port_name == gpio_port_b) {
        // set LAT for this pin
        LATBCLR = (1 << pin_number);
        LATBSET = (lat_setting << pin_number);
        // set ODC for this pin
        ODCBCLR = (1 << pin_number);
        ODCBSET = (open_drain_setting << pin_number);
        // set ANSEL for this pin
        #ifdef ANSELB
        ANSELBCLR = (1 << pin_number);
        ANSELBSET = (analog_setting << pin_number);
        #endif
        // Set TRIS for this pin
        TRISBCLR = (1 << pin_number);
        TRISBSET = (tris_setting << pin_number);
    }
    #endif

    #ifdef LATC
    if (port_name == gpio_port_c) {
        // set LAT for this pin
        LATCCLR = (1 << pin_number);
        LATCSET = (lat_setting << pin_number);
        // set ODC for this pin
        ODCCCLR = (1 << pin_number);
        ODCCSET = (open_drain_setting << pin_number);
        // set ANSEL for this pin
        #ifdef ANSELC
        ANSELCCLR = (1 << pin_number);
        ANSELCSET = (analog_setting << pin_number);
        #endif
        // Set TRIS for this pin
        TRISCCLR = (1 << pin_number);
        TRISCSET = (tris_setting << pin_number);
    }
    #endif

    #ifdef LATD
    if (port_name == gpio_port_d) {
        // set LAT for this pin
        LATDCLR = (1 << pin_number);
        LATDSET = (lat_setting << pin_number);
        // set ODC for this pin
        ODCDCLR = (1 << pin_number);
        ODCDSET = (open_drain_setting << pin_number);
        // set ANSEL for this pin
        #ifdef ANSELD
        ANSELDCLR = (1 << pin_number);
        ANSELDSET = (analog_setting << pin_number);
        #endif
        // Set TRIS for this pin
        TRISDCLR = (1 << pin_number);
        TRISDSET = (tris_setting << pin_number);
    }
    #endif

    #ifdef LATE
    if (port_name == gpio_port_e) {
        // set LAT for this pin
        LATECLR = (1 << pin_number);
        LATESET = (lat_setting << pin_number);
        // set ODC for this pin
        ODCECLR = (1 << pin_number);
        ODCESET = (open_drain_setting << pin_number);
        // set ANSEL for this pin
        #ifdef ANSELE
        ANSELECLR = (1 << pin_number);
        ANSELESET = (analog_setting << pin_number);
        #endif
        // Set TRIS for this pin
        TRISECLR = (1 << pin_number);
        TRISESET = (tris_setting << pin_number);
    }
    #endif

    #ifdef LATF
    if (port_name == gpio_port_f) {
        // set LAT for this pin
        LATFCLR = (1 << pin_number);
        LATFSET = (lat_setting << pin_number);
        // set ODC for this pin
        ODCFCLR = (1 << pin_number);
        ODCFSET = (open_drain_setting << pin_number);
        // set ANSEL for this pin
        #ifdef ANSELF
        ANSELFCLR = (1 << pin_number);
        ANSELFSET = (analog_setting << pin_number);
        #endif
        // Set TRIS for this pin
        TRISFCLR = (1 << pin_number);
        TRISFSET = (tris_setting << pin_number);
    }
    #endif

    #ifdef LATG
    if (port_name == gpio_port_g) {
        // set LAT for this pin
        LATGCLR = (1 << pin_number);
        LATGSET = (lat_setting << pin_number);
        // set ODC for this pin
        ODCGCLR = (1 << pin_number);
        ODCGSET = (open_drain_setting << pin_number);
        // set ANSEL for this pin
        #ifdef ANSELG
        ANSELGCLR = (1 << pin_number);
        ANSELGSET = (analog_setting << pin_number);
        #endif
        // Set TRIS for this pin
        TRISGCLR = (1 << pin_number);
        TRISGSET = (tris_setting << pin_number);
    }
    #endif

    #ifdef LATH
    if (port_name == gpio_port_h) {
        // set LAT for this pin
        LATHCLR = (1 << pin_number);
        LATHSET = (lat_setting << pin_number);
        // set ODC for this pin
        ODCHCLR = (1 << pin_number);
        ODCHSET = (open_drain_setting << pin_number);
        // set ANSEL for this pin
        #ifdef ANSELH
        ANSELHCLR = (1 << pin_number);
        ANSELHSET = (analog_setting << pin_number);
        #endif
        // Set TRIS for this pin
        TRISHCLR = (1 << pin_number);
        TRISHSET = (tris_setting << pin_number);
    }
    #endif

    #ifdef LATJ
    if (port_name == gpio_port_j) {
        // set LAT for this pin
        LATJCLR = (1 << pin_number);
        LATJSET = (lat_setting << pin_number);
        // set ODC for this pin
        ODCJCLR = (1 << pin_number);
        ODCJSET = (open_drain_setting << pin_number);
        // set ANSEL for this pin
        #ifdef ANSELJ
        ANSELJCLR = (1 << pin_number);
        ANSELJSET = (analog_setting << pin_number);
        #endif
        // Set TRIS for this pin
        TRISJCLR = (1 << pin_number);
        TRISJSET = (tris_setting << pin_number);
    }
    #endif

    #ifdef LATK
    if (port_name == gpio_port_k) {
        // set LAT for this pin
        LATKCLR = (1 << pin_number);
        LATKSET = (lat_setting << pin_number);
        // set ODC for this pin
        ODCKCLR = (1 << pin_number);
        ODCKSET = (open_drain_setting << pin_number);
        // set ANSEL for this pin
        #ifdef ANSELK
        ANSELKCLR = (1 << pin_number);
        ANSELKSET = (analog_setting << pin_number);
        #endif
        // Set TRIS for this pin
        TRISKCLR = (1 << pin_number);
        TRISKSET = (tris_setting << pin_number);
    }
    #endif

}

// initializes GPIO ports on microcontroller
bool gpioInitialize (void) {
    
    // Unlock peripheral pin select
    PPSUnlock();
    
    portAGPIOInitialize();
    portBGPIOInitialize();
    portCGPIOInitialize();
    portDGPIOInitialize();
    portEGPIOInitialize();
    portFGPIOInitialize();
    portGGPIOInitialize();
    portHGPIOInitialize();
    portJGPIOInitialize();
    portKGPIOInitialize();
    
    // Lock PPS
    PPSLock();

    // Pin configuration has no software-detectable failure mode
    return true;

}

// Register set for one GPIO port, plus which pins are bonded out on the
// 176-pin package (PORTx bits for unimplemented pins read undefined)
typedef struct {
    const char *name;
    uint16_t pin_mask;
    volatile uint32_t *ansel;
    volatile uint32_t *tris;
    volatile uint32_t *port;
    volatile uint32_t *lat;
    volatile uint32_t *odc;
    volatile uint32_t *cnpu;
    volatile uint32_t *cnpd;
    volatile uint32_t *cncon;
    volatile uint32_t *cnen;
    volatile uint32_t *cnstat;
    volatile uint32_t *cnne;
    volatile uint32_t *cnf;
    volatile uint32_t *srcon0;
    volatile uint32_t *srcon1;
} gpio_port_registers_t;

#define GPIO_PORT_ENTRY(letter, mask)                                           \
    {   #letter, (mask),                                                        \
        &ANSEL##letter, &TRIS##letter, &PORT##letter, &LAT##letter,             \
        &ODC##letter, &CNPU##letter, &CNPD##letter, &CNCON##letter,             \
        &CNEN##letter, &CNSTAT##letter, &CNNE##letter, &CNF##letter,            \
        &SRCON0##letter, &SRCON1##letter }

#define GPIO_GRID_LABEL_WIDTH 9
#define GPIO_GRID_COL_WIDTH   5

// prints one grid row: a label followed by one value per active pin,
// right-justified in fixed-width columns so rows line up under the header
static void printGPIOGridRow(const char *label, const char *values[16], uint16_t pin_mask) {

    uint32_t pin;

    printf("%-*s", GPIO_GRID_LABEL_WIDTH, label);
    for (pin = 0; pin <= 15; pin++) {
        if (!((pin_mask >> pin) & 0x1)) continue;
        printf("%*s", GPIO_GRID_COL_WIDTH, values[pin]);
    }
    printf("\r\n");

}

// prints the configuration and live state of every GPIO pin on every port,
// laid out as a grid (columns = pins, rows = settings) for easy comparison
void printGPIOPortsStatus(void) {

    static const gpio_port_registers_t gpio_ports[] = {
        GPIO_PORT_ENTRY(A, 0xC6FF),     // RA0-7, RA9, RA10, RA14, RA15
        GPIO_PORT_ENTRY(B, 0xFFFF),     // RB0-15
        GPIO_PORT_ENTRY(C, 0xF01E),     // RC1-4, RC12-15
        GPIO_PORT_ENTRY(D, 0xFFFF),     // RD0-15
        GPIO_PORT_ENTRY(E, 0x03FF),     // RE0-9
        GPIO_PORT_ENTRY(F, 0x313F),     // RF0-5, RF8, RF12, RF13
        GPIO_PORT_ENTRY(G, 0xF3C3),     // RG0, RG1, RG6-9, RG12-15
        GPIO_PORT_ENTRY(H, 0xFFFF),     // RH0-15
        GPIO_PORT_ENTRY(J, 0xFFFF),     // RJ0-15
        GPIO_PORT_ENTRY(K, 0x00FF),     // RK0-7
    };

    // Slew rate select is 2 bits per pin, {SRCON1<n>, SRCON0<n>}
    static const char *slew_strings[4] = {"Fst", "Med", "Slo", "Slw"};

    uint32_t port_index;
    uint32_t pin;

    terminalTextAttributesReset();
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("GPIO Ports Status:\r\n");
    terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Mode: In/Out/Ana   Drive: PP=push-pull, OD=open-drain   PU/PD: weak pull-up/down (--=off)\r\n");
    printf("    CN: R/F/RF=edge rising/falling/both, M=mismatch, --=off, '!' = change/interrupt flag set\r\n");
    printf("    Slew: Fst/Med/Slo/Slw = fastest..slowest output slew rate\r\n");

    for (port_index = 0; port_index < sizeof(gpio_ports) / sizeof(gpio_ports[0]); port_index++) {

        const gpio_port_registers_t *p = &gpio_ports[port_index];

        // snapshot the port's registers so the whole grid is self-consistent
        uint32_t ansel  = *p->ansel;
        uint32_t tris   = *p->tris;
        uint32_t port   = *p->port;
        uint32_t lat    = *p->lat;
        uint32_t odc    = *p->odc;
        uint32_t cnpu   = *p->cnpu;
        uint32_t cnpd   = *p->cnpd;
        uint32_t cncon  = *p->cncon;
        uint32_t cnen   = *p->cnen;
        uint32_t cnstat = *p->cnstat;
        uint32_t cnne   = *p->cnne;
        uint32_t cnf    = *p->cnf;
        uint32_t srcon0 = *p->srcon0;
        uint32_t srcon1 = *p->srcon1;

        uint32_t cn_module_on  = (cncon >> 15) & 0x1;
        uint32_t cn_edge_style = (cncon >> 11) & 0x1;

        // per-pin value strings for this port, filled in below and handed
        // to printGPIOGridRow() one row at a time
        char pin_num_str[16][4];
        char mode_str[16][4];
        char port_str[16][2];
        char lat_str[16][2];
        char drive_str[16][3];
        char pu_str[16][3];
        char pd_str[16][3];
        char cn_str[16][4];
        const char *slew_str[16];
        const char *row[16];

        for (pin = 0; pin <= 15; pin++) {

            if (!((p->pin_mask >> pin) & 0x1)) continue;

            snprintf(pin_num_str[pin], sizeof(pin_num_str[pin]), "%u", pin);

            if ((ansel >> pin) & 0x1)      strcpy(mode_str[pin], "Ana");
            else if ((tris >> pin) & 0x1)  strcpy(mode_str[pin], "In");
            else                           strcpy(mode_str[pin], "Out");

            snprintf(port_str[pin], sizeof(port_str[pin]), "%u", (unsigned) (port >> pin) & 0x1);
            snprintf(lat_str[pin], sizeof(lat_str[pin]), "%u", (unsigned) (lat >> pin) & 0x1);
            strcpy(drive_str[pin], ((odc >> pin) & 0x1) ? "OD" : "PP");
            strcpy(pu_str[pin], ((cnpu >> pin) & 0x1) ? "On" : "--");
            strcpy(pd_str[pin], ((cnpd >> pin) & 0x1) ? "On" : "--");

            // Change notification setting for this pin: in edge detect mode
            // CNEN/CNNE enable rising/falling edges and CNF holds the flag,
            // in mismatch mode CNEN enables the pin and CNSTAT holds the flag
            uint32_t cn_flag;
            if (cn_edge_style) {
                uint32_t rising  = (cnen >> pin) & 0x1;
                uint32_t falling = (cnne >> pin) & 0x1;
                if (rising && falling)  strcpy(cn_str[pin], "RF");
                else if (rising)        strcpy(cn_str[pin], "R");
                else if (falling)       strcpy(cn_str[pin], "F");
                else                    strcpy(cn_str[pin], "--");
                cn_flag = (cnf >> pin) & 0x1;
            }
            else {
                strcpy(cn_str[pin], ((cnen >> pin) & 0x1) ? "M" : "--");
                cn_flag = (cnstat >> pin) & 0x1;
            }
            if (cn_flag) strcat(cn_str[pin], "!");

            uint32_t slew_select = (((srcon1 >> pin) & 0x1) << 1) | ((srcon0 >> pin) & 0x1);
            slew_str[pin] = slew_strings[slew_select];

        }

        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
        if (cn_module_on) {
            printf("\r\nPort %s (Change Notification On, %s mode):\r\n",
                    p->name,
                    cn_edge_style ? "edge detect" : "mismatch");
        }
        else {
            printf("\r\nPort %s (Change Notification Off):\r\n", p->name);
        }
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);

        for (pin = 0; pin <= 15; pin++) row[pin] = pin_num_str[pin];
        printGPIOGridRow("Pin", row, p->pin_mask);

        for (pin = 0; pin <= 15; pin++) row[pin] = mode_str[pin];
        printGPIOGridRow("Mode", row, p->pin_mask);

        for (pin = 0; pin <= 15; pin++) row[pin] = port_str[pin];
        printGPIOGridRow("State", row, p->pin_mask);

        for (pin = 0; pin <= 15; pin++) row[pin] = lat_str[pin];
        printGPIOGridRow("Latch", row, p->pin_mask);

        for (pin = 0; pin <= 15; pin++) row[pin] = drive_str[pin];
        printGPIOGridRow("Drive", row, p->pin_mask);

        for (pin = 0; pin <= 15; pin++) row[pin] = pu_str[pin];
        printGPIOGridRow("Pull-Up", row, p->pin_mask);

        for (pin = 0; pin <= 15; pin++) row[pin] = pd_str[pin];
        printGPIOGridRow("Pull-Dn", row, p->pin_mask);

        for (pin = 0; pin <= 15; pin++) row[pin] = cn_str[pin];
        printGPIOGridRow("CN", row, p->pin_mask);

        for (pin = 0; pin <= 15; pin++) row[pin] = slew_str[pin];
        printGPIOGridRow("Slew", row, p->pin_mask);

    }

    terminalTextAttributesReset();

}
