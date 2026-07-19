/*******************************************************************************
  GLT035320240IS1-CTP Panel Timing/Reset Driver

  File Name:
    glt035320240is1.h

  Summary:
    Panel-specific constants and reset sequencing for the GlobalTech Display
    GLT035320240IS1-CTP (3.5", 320x240, 24-bit RGB, ST7272A driver).

  Description:
    This is the panel-specific layer over glcd/glcd.c, mirroring the
    sdhc/device_driver/sd_card.c split: glcd.c is a reusable register-level
    PIC32MZ GLCD Controller driver, this file knows the one panel actually
    wired to it.

    Timing values below come from the panel's own datasheet ("TFT LCD
    Display Specification", PN GLT035320240IS1-CTP, Rev 2.0 7/15/2021,
    pulled from gtdisplays.com, linked from its DigiKey listing), section
    6.3 "24 Bit RGB Mode" parallel input timing table:
      DCLK: 5-8MHz, typ 6MHz
      HSYNC: period 325-438 (typ 371) DCLK, display 320 DCLK, back porch
             3-43 DCLK (43 REQUIRED in sync mode), front porch 2-75 (typ 8)
             DCLK, pulse width 2-43 (typ 4) DCLK
      VSYNC: period 244-289 (typ 260) lines, display 240 lines, back porch
             2-12 lines (12 REQUIRED in sync mode), front porch 2-37 (typ 8)
             lines, pulse width 2-12 (typ 4) lines
    The datasheet explicitly states back porch must be kept at these exact
    values ("necessary to keep Tvbp=12 and Thbp=43 in sync mode") -- this
    driver runs HSYNC/VSYNC/GCLK/GEN together (not DE-only mode), so that
    constraint applies.

    These are translated into the GLCD Controller's front-porch/blanking/
    back-porch register convention (glcd/glcd.h, matching PIC32 Family
    Reference Manual Section 54 Equations 54-1 through 54-6):
      FPORCHx  = RESx + front_porch
      BLANKINGx = FPORCHx + pulse_width
      BPORCHx  = BLANKINGx + back_porch

    Reset ("LCD_ENABLE" in the schematic, despite the name -- confirmed via
    KiCad net trace: J2101 pin 8 = panel RESET, active-low, "Low is enable"
    per the panel datasheet's I/O terminal table) must be held low for at
    least Tasta = 40us (panel datasheet section 6.1 AC Electrical
    Characteristics) before being released.

    Polarity (HSYNCPOL/VSYNCPOL/DEPOL/PCLKPOL) is NOT specified for this
    panel in its datasheet -- only generic waveform diagrams are given. The
    values below are the common ST7272A default (active-low HSYNC/VSYNC,
    active-high DE, data latched on the rising DCLK edge). If the test
    pattern added in a later step comes out shifted/rolled/scrambled, this
    is the first thing to flip -- it's a display-only symptom, not a
    hardware risk.
*******************************************************************************/

#ifndef GLT035320240IS1_H
#define GLT035320240IS1_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Panel resolution
#define GLT035320240IS1_WIDTH_PX           320
#define GLT035320240IS1_HEIGHT_PX          240

// GLCD Controller front-porch/blanking/back-porch register values, derived
// per the equations above from the panel's typical timing (front porch,
// pulse width) and its two REQUIRED back-porch values (Thbp=43, Tvbp=12)
#define GLT035320240IS1_FPORCHX            328     // 320 + 8
#define GLT035320240IS1_FPORCHY            248     // 240 + 8
#define GLT035320240IS1_BLANKINGX          332     // 328 + 4
#define GLT035320240IS1_BLANKINGY          252     // 248 + 4
#define GLT035320240IS1_BPORCHX            375     // 332 + 43 (required)
#define GLT035320240IS1_BPORCHY            264     // 252 + 12 (required)

// Panel reset pulse width requirement (Tasta, panel datasheet 6.1)
#define GLT035320240IS1_RESET_PULSE_US     40u

// Drives LCD_ENABLE_PIN (gpio/pin_macros.h, the panel's active-low RESET,
// J2101 pin 8) low for GLT035320240IS1_RESET_PULSE_US, then releases it
// high. Caller (GLCD_Initialize()) must do this before enabling the GLCD
// Controller (GLCDMODE.LCDEN).
void GLT035320240IS1_ResetPulse(void);

#ifdef __cplusplus
}
#endif

#endif /* GLT035320240IS1_H */
