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
void GT911_PrintStatus(uint16_t address);

#ifdef __cplusplus
}
#endif

#endif /* GT911_H */
