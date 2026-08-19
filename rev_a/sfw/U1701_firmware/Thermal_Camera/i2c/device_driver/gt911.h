/*******************************************************************************
  GT911 I2C Capacitive Touch Controller Driver

  File Name:
    gt911.h

  Summary:
    Driver for the Goodix GT911 5-point capacitive touch controller
    integrated into the GLT035320240IS1-CTP LCD module, built on
    i2c_master.h.

  Description:
    This driver only brings the GT911 up to a known I2C address and
    confirms its presence/identity -- it deliberately implements no touch
    coordinate reporting (interrupt handling, touch-point registers, etc).
    Its presence is used elsewhere (main.c) as a proxy for "the LCD module
    is actually populated on this board", since the GLCD Controller itself
    (glcd/glcd.c) has no way to detect whether a panel is physically
    attached -- it only configures MCU-internal registers.

    Address selection: unlike every other I2C device on this board
    (MCP9804/INA231A/DS1683), the GT911 does not have a fixed or
    pin-strapped address. Per the GT911 datasheet (Goodix, Rev.09
    2015-03-11) Section 6.1, the host selects one of two address pairs
    (0xBA/0xBB or 0x28/0x29, 8-bit) by toggling the RESET and INT pins in a
    specific sequence during every power-up AND during any host-initiated
    reset -- the address does not persist across a reset on its own.
    GT911_Verify() runs that sequence (GT911_ResetAndSelectAddress(),
    internal to gt911.c) every time it's called, selecting 7-bit address
    0x14 (8-bit 0x28/0x29) -- which matches the panel datasheet
    (GLT035320240IS1-CTP, section on CTP notes: "IIC address: 0x28").

    Register addressing: the GT911 uses 16-bit, big-endian register
    addresses (send Register_H then Register_L, per the datasheet's Write/
    Read Operation timing diagrams) -- NOT the single 8-bit register byte
    every other device on this board uses, so this driver cannot use
    i2c_master.h's I2C_ReadRegister()/I2C_WriteRegister() helpers.

    Register map: Goodix's official datasheet deliberately removed the
    register map starting at Rev.07 (per its revision history) with no
    replacement published since. The registers used below (Product ID
    0x8140, Config Version 0x8047, Firmware Version 0x8144) are cross-
    checked against a second, independent, reputable source
    (STMicroelectronics' stm32-gt911 driver, gt911_reg.h) rather than
    relied on from a single unofficial source.
*******************************************************************************/

#ifndef GT911_H
#define GT911_H

#include <stdint.h>
#include <stdbool.h>

#include "i2c/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

// 7-bit I2C address this driver's reset/address-select sequence always
// selects (8-bit write/read 0x28/0x29). See the file header for the
// selection mechanism -- this is not a fixed hardware address the way
// DS1683_BASE_ADDRESS is.
#define GT911_ADDRESS   0x14u

// Runs the GT911's I2C-address-select sequence (see file header), then
// confirms presence by reading the Product ID register and checking for
// the expected "911" ASCII string. Every call re-runs the full reset
// sequence (~65ms), since the address does not persist across a GT911
// reset on its own.
bool GT911_Verify(uint16_t address);

// Prints the device's Product ID, Config Version, and Firmware Version
// registers (whichever are readable), plus the live level of
// LCD_CTP_RESET_PIN/LCD_CTP_INT_PIN. Read-only, like every other device
// driver's PrintStatus() in this codebase -- does NOT re-run the reset/
// address-select sequence, so it reflects whatever GT911_Verify() (or the
// last reset) already established.
// Everything GT911_PrintStatus() shows, as data: the identity registers and
// the coordinate status byte. Exists so the GUI's I2C status screen can show
// the same diagnostics the console does without either one re-deriving the
// register map -- GT911_PrintStatus() is written on top of this.
//
// STRICTLY READ-ONLY, and that is load-bearing rather than incidental.
// GT911_ReadTouch() acknowledges a report by WRITING the coordinate status
// register back to zero, and gui/lv_port_indev.c must be the only caller
// doing that -- anything else acknowledging a report consumes a touch the
// input driver never sees. This function only reads that register, so it can
// run alongside the input driver without stealing from it.
typedef struct
{
    bool productIdValid;     // false = no response; nothing else is filled in
    uint8_t productId[4];
    bool identified;         // product ID reads the expected "911"

    bool configVersionValid;
    uint8_t configVersion;

    bool firmwareVersionValid;
    uint8_t firmwareVersion[2];

    bool coordStatusValid;
    uint8_t coordStatus;
} GT911_DIAGNOSTICS;

// Fills `out` with the above. Returns productIdValid, i.e. whether the
// device answered at all. Does NOT re-run the reset/address-select sequence
// (unlike GT911_Verify()), and does not acknowledge a touch report.
bool GT911_ReadDiagnostics(uint16_t address, GT911_DIAGNOSTICS *out);

void GT911_PrintStatus(uint16_t address);

// A single touch point, in the controller's own coordinate space (which is
// whatever its config registers were programmed with -- see the mapping note
// in gui/lv_port_indev.h, which is what turns these into panel pixels).
typedef struct
{
    bool pressed;       // false = the new report says every finger is up
    uint16_t x;
    uint16_t y;
} GT911_TOUCH;

typedef enum
{
    GT911_TOUCH_NO_NEW_DATA = 0,   // controller has nothing new; keep last state
    GT911_TOUCH_UPDATED,           // `touch` was filled in from a fresh report
    GT911_TOUCH_ERROR              // I2C read/write failed
} GT911_TOUCH_RESULT;

// Polls the coordinate registers for a new touch report, returning only the
// first touch point -- this driver is deliberately single-touch, since every
// GUI interaction in this firmware is a tap (see gui/lv_port_indev.c).
//
// The GT911 raises a BUFFER_READY flag when a report is ready and expects
// the host to clear the status register to acknowledge it; this function
// does both, so it must be the only caller polling that register. Between
// reports it returns GT911_TOUCH_NO_NEW_DATA and leaves `touch` untouched
// -- a held finger produces reports continuously, so "no new data" means
// "nothing changed", NOT "released".
//
// Polling rather than using the INT line is deliberate: the panel's INT pin
// is shared with the address-select sequence (GT911_Verify() drives it as a
// GPIO and leaves it floating), and a ~30ms poll from the LVGL input timer
// is well inside human tap timing.
GT911_TOUCH_RESULT GT911_ReadTouch(uint16_t address, GT911_TOUCH *touch);

#ifdef __cplusplus
}
#endif

#endif /* GT911_H */
