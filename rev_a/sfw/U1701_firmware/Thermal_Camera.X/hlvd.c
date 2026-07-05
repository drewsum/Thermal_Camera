
#include <xc.h>
#include <stdio.h>

#include "hlvd.h"
#include "terminal_control.h"

// This function disables the module, applies the requested trip point and
// direction, then re-enables the module. Per the HLVD setup procedure,
// settings may only be changed while the module is off.
void hlvdInitialize(uint8_t trip_point, hlvd_direction_t direction) {

    HLVDCONbits.ON = 0;

    HLVDCONbits.HLVDL = trip_point;
    HLVDCONbits.VDIR = direction;

    // continue operation while the CPU is in Idle mode
    HLVDCONbits.SIDL = 0;

    HLVDCONbits.ON = 1;

}

// This function disables the HLVD module
void hlvdDisable(void) {

    HLVDCONbits.ON = 0;

}

// This function returns 1 if the band gap reference has stabilized
// (HLVDCONbits.BGVST), meaning HLEVT can be trusted, 0 otherwise
uint8_t hlvdIsReady(void) {

    return HLVDCONbits.BGVST;

}

// This function returns the live HLVD event status bit (HLEVT). This
// tracks the comparator in real time and clears itself if VDD moves back
// away from the trip point -- treat it as a level, not a latch.
uint8_t hlvdCheckEvent(void) {

    return HLVDCONbits.HLEVT;

}

// This function reads the latched HLVD flag (RNMICONbits.LVD), clears it,
// and returns the value it had before clearing.
uint8_t hlvdCheckAndClearLatchedEvent(void) {

    uint8_t previous_state = RNMICONbits.LVD;

    RNMICONbits.LVD = 0;

    return previous_state;

}

// This function reads the VDD18/core high/low-voltage detect flag
// (RCONbits.HVDCORE), clears it, and returns the value it had before
// clearing.
uint8_t hlvdCoreCheckAndClearEvent(void) {

    uint8_t previous_state = RCONbits.HVDCORE;

    RCONbits.HVDCORE = 0;

    return previous_state;

}

// This function prints the current HLVD configuration and status
void printHLVDStatus(void) {

    terminalTextAttributesReset();
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("High/Low-Voltage Detect Status:\n\r");

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Module Enabled: %c\n\r", HLVDCONbits.ON ? 'T' : 'F');
    printf("    Trip Point Select (HLVDL): %d\n\r", HLVDCONbits.HLVDL);
    printf("    Direction: %s\n\r", HLVDCONbits.VDIR ?
            "High-voltage detect (event when VDD >= trip point)" :
            "Low-voltage detect (event when VDD <= trip point)");
    printf("    Band Gap Reference Stable: %c\n\r", HLVDCONbits.BGVST ? 'T' : 'F');

    if (HLVDCONbits.HLEVT) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    HLVD Event Active (HLEVT): %c\n\r", HLVDCONbits.HLEVT ? 'T' : 'F');

    if (RNMICONbits.LVD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Latched HLVD Event (RNMICON.LVD): %c\n\r", RNMICONbits.LVD ? 'T' : 'F');

    if (RCONbits.HVDCORE) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    VDD18/Core HVD Event (RCON.HVDCORE, BOR companion bit): %c\n\r", RCONbits.HVDCORE ? 'T' : 'F');

    terminalTextAttributesReset();

}
