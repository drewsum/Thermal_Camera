/*******************************************************************************
  GT911 I2C Capacitive Touch Controller Driver

  File Name:
    gt911.c

  Summary:
    Driver for the Goodix GT911 touch controller. See gt911.h for the
    address-selection and register-addressing design notes.
*******************************************************************************/

#include <xc.h>
#include <stdio.h>
#include <string.h>

#include "i2c/device_driver/gt911.h"
#include "i2c/i2c_master.h"
#include "core/device_control.h"
#include "gpio/pin_macros.h"
#include "gpio/pic32mzda_gpio_setup.h"
#include "usb_uart/terminal_control.h"

// Register addresses -- see the file header in gt911.h for sourcing.
#define GT911_REG_PRODUCT_ID           0x8140u   // 4 bytes: ASCII "911" + 0x00
#define GT911_REG_CONFIG_VERSION       0x8047u   // 1 byte
#define GT911_REG_FIRMWARE_VERSION     0x8144u   // 2 bytes
#define GT911_REG_COORD_STATUS         0x814Eu   // 1 byte, touch/buffer status

// Touch point records start immediately after the status register and are 8
// bytes each: track ID, X low, X high, Y low, Y high, size low, size high,
// reserved. Only point 1 is read (see GT911_ReadTouch() in gt911.h), and
// only its 4 coordinate bytes -- so this address is the point record base
// PLUS the one-byte track ID. Same sourcing caveat as the registers above:
// Goodix removed the map from the datasheet at Rev.07, so this is
// cross-checked against STMicroelectronics' stm32-gt911 driver (gt911_reg.h)
// rather than taken from a single unofficial source.
#define GT911_REG_POINT1_COORDS        0x8150u   // 4 bytes: XL, XH, YL, YH

// Coordinate status register (0x814E) bitfield. The controller sets
// BUFFER_READY when a fresh touch report is available and the host clears
// the register to acknowledge it; the low nibble is the number of active
// touch points.
#define GT911_STATUS_BUFFER_READY      0x80u
#define GT911_STATUS_LARGE_DETECT      0x40u
#define GT911_STATUS_HAVE_KEY          0x10u
#define GT911_STATUS_POINT_COUNT_MASK  0x0Fu

static const uint8_t GT911_EXPECTED_PRODUCT_ID[4] = { '9', '1', '1', 0x00u };

#define GT911_TIMEOUT_TICKS(us)    ((uint32_t)(((uint64_t)SYSCLK_INT / 2u) * (us) / 1000000u))

// Calibrated microsecond delay via CP0 Count (increments at SYSCLK/2) --
// same pattern as SD_Card_DelayUs()/GLT035320240IS1_DelayUs(), since the
// address-select sequence's timing minimums (from the GT911 datasheet) are
// real hardware requirements, not rough settling guesses.
static void GT911_DelayUs(uint32_t us)
{
    uint32_t start = _CP0_GET_COUNT();
    uint32_t ticks = GT911_TIMEOUT_TICKS(us);
    while ((uint32_t)(_CP0_GET_COUNT() - start) < ticks)
    {
        // busy-wait
    }
}

// GT911 uses 16-bit, big-endian register addressing (Register_H then
// Register_L) -- see gt911.h for why this can't use I2C_ReadRegister().
static bool GT911_ReadRegister16(uint16_t address, uint16_t reg, uint8_t *data, size_t length)
{
    uint8_t regBytes[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFFu) };

    return I2C_WriteRead(address, regBytes, sizeof(regBytes), data, length);
}

// Companion to GT911_ReadRegister16 -- the address bytes and the payload go
// out as ONE write (the GT911 has no repeated-start write path), so this
// stages them into a single buffer. Only ever used to clear the one-byte
// coordinate status register, hence the deliberately small staging buffer.
#define GT911_WRITE_MAX_PAYLOAD   4u

static bool GT911_WriteRegister16(uint16_t address, uint16_t reg, const uint8_t *data, size_t length)
{
    uint8_t buffer[2 + GT911_WRITE_MAX_PAYLOAD];
    size_t i;

    if (length > GT911_WRITE_MAX_PAYLOAD) return false;

    buffer[0] = (uint8_t)(reg >> 8);
    buffer[1] = (uint8_t)(reg & 0xFFu);

    for (i = 0; i < length; i++) buffer[2 + i] = data[i];

    return I2C_Write(address, buffer, 2 + length);
}

// GT911 I2C address-select sequence, from the GT911 datasheet (Goodix,
// Rev.09, 2015-03-11) Section 6.1, "Timing for setting slave address to
// 0x28/0x29":
//     1. RESET low
//     2. INT high              (selects 0x28/0x29 = 7-bit 0x14)
//     3. RESET high             >100us after step 2 (datasheet minimum)
//     4. INT low                >5ms after step 3 (datasheet minimum)
//     5. INT set to floating input   >50ms after step 4 (datasheet minimum)
// The datasheet's diagram doesn't give an explicit minimum hold time
// before step 2 (it assumes RESET/INT already low from power-on) -- 10ms
// is used here, matching the same datasheet's separate Power-on Timing
// diagram (T2>10ms, VDDIO-stable-to-address-select settle time), applied
// conservatively as the low-hold duration for a host-initiated reset too.
// This sequence must run before ANY I2C traffic to this device is
// possible, and must be re-run on every reset (the address selection does
// not persist).
static void GT911_ResetAndSelectAddress(void)
{
    // RJ8 (LCD_CTP_INT_PIN) is normally an input -- switch it to a GPIO
    // output for the duration of this sequence only.
    gpioPinSetup(gpio_port_j, 8, TRIS_OUTPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);

    // Step 1: hold both RESET and INT low
    LCD_CTP_RESET_PIN = LOW;
    LCD_CTP_INT_LAT_PIN = LOW;
    GT911_DelayUs(10000u);

    // Step 2: INT high selects 0x28/0x29 (7-bit 0x14)
    LCD_CTP_INT_LAT_PIN = HIGH;
    GT911_DelayUs(100u);

    // Step 3: release RESET
    LCD_CTP_RESET_PIN = HIGH;
    GT911_DelayUs(5000u);

    // Step 4: INT low
    LCD_CTP_INT_LAT_PIN = LOW;
    GT911_DelayUs(50000u);

    // Step 5: INT becomes the interrupt output -- restore it to input
    // (floating), matching gpioInitialize()'s normal configuration for
    // this pin. This driver never uses the touch interrupt.
    gpioPinSetup(gpio_port_j, 8, TRIS_INPUT, LAT_LOW, ODC_DISABLE, ANALOG_DISABLE);
}

bool GT911_Verify(uint16_t address)
{
    uint8_t productId[4];

    GT911_ResetAndSelectAddress();

    if (!GT911_ReadRegister16(address, GT911_REG_PRODUCT_ID, productId, sizeof(productId)))
    {
        return false;
    }

    return (memcmp(productId, GT911_EXPECTED_PRODUCT_ID, sizeof(productId)) == 0);
}

GT911_TOUCH_RESULT GT911_ReadTouch(uint16_t address, GT911_TOUCH *touch)
{
    uint8_t status;
    uint8_t coords[4];
    uint8_t clear = 0;
    GT911_TOUCH_RESULT result;

    if (!GT911_ReadRegister16(address, GT911_REG_COORD_STATUS, &status, 1))
    {
        return GT911_TOUCH_ERROR;
    }

    // No report pending. This is by far the common case -- the controller
    // only raises BUFFER_READY when something changed -- so it costs exactly
    // the one status byte read above and no write.
    if ((status & GT911_STATUS_BUFFER_READY) == 0u)
    {
        return GT911_TOUCH_NO_NEW_DATA;
    }

    if ((status & GT911_STATUS_POINT_COUNT_MASK) != 0u)
    {
        if (GT911_ReadRegister16(address, GT911_REG_POINT1_COORDS, coords, sizeof(coords)))
        {
            // Little-endian pairs, unlike the big-endian REGISTER addressing
            // this same device uses -- see the file header in gt911.h.
            touch->x = (uint16_t)coords[0] | ((uint16_t)coords[1] << 8);
            touch->y = (uint16_t)coords[2] | ((uint16_t)coords[3] << 8);
            touch->pressed = true;
            result = GT911_TOUCH_UPDATED;
        }
        else
        {
            result = GT911_TOUCH_ERROR;
        }
    }
    else
    {
        // A report with zero points IS the release event -- the coordinates
        // are stale at this point, so only the flag is updated and the last
        // position is left in place (LVGL wants the release to land at the
        // point the finger lifted from, not at 0,0).
        touch->pressed = false;
        result = GT911_TOUCH_UPDATED;
    }

    // Acknowledge the report unconditionally, including on the read failure
    // above: leaving the status register set means the controller never
    // posts another report and touch dies silently until the next reset.
    if (!GT911_WriteRegister16(address, GT911_REG_COORD_STATUS, &clear, 1))
    {
        result = GT911_TOUCH_ERROR;
    }

    return result;
}

void GT911_PrintStatus(uint16_t address)
{
    uint8_t productId[4];
    uint8_t configVersion;
    uint8_t firmwareVersion[2];
    uint8_t coordStatus;
    bool gotProductId, gotConfigVersion, gotFirmwareVersion;

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    --- GT911 (address 0x%02X) ---\n\r", address);

    gotProductId = GT911_ReadRegister16(address, GT911_REG_PRODUCT_ID, productId, sizeof(productId));
    gotConfigVersion = GT911_ReadRegister16(address, GT911_REG_CONFIG_VERSION, &configVersion, 1);
    gotFirmwareVersion = GT911_ReadRegister16(address, GT911_REG_FIRMWARE_VERSION, firmwareVersion, sizeof(firmwareVersion));

    if (gotProductId)
    {
        bool identified = (memcmp(productId, GT911_EXPECTED_PRODUCT_ID, sizeof(productId)) == 0);
        terminalTextAttributes(identified ? GREEN_COLOR : RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    Product ID: %c%c%c (0x%02X 0x%02X 0x%02X 0x%02X) %s\n\r",
                productId[0], productId[1], productId[2],
                productId[0], productId[1], productId[2], productId[3],
                identified ? "as expected" : "unexpected -- not a GT911?");
    }
    else
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    No response from device (I2C error: %d)\n\r", (int)I2C_ErrorGet());
        terminalTextAttributesReset();
        return;
    }

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    if (gotConfigVersion) printf("    Config Version: 0x%02X\n\r", configVersion);
    else printf("    Config Version: read failed\n\r");

    if (gotFirmwareVersion) printf("    Firmware Version: 0x%02X%02X\n\r", firmwareVersion[0], firmwareVersion[1]);
    else printf("    Firmware Version: read failed\n\r");

    if (GT911_ReadRegister16(address, GT911_REG_COORD_STATUS, &coordStatus, 1))
    {
        printf("    Coordinate Status: 0x%02X (%s%s%s%u point%s)\n\r", coordStatus,
               (coordStatus & GT911_STATUS_BUFFER_READY) ? "BUFFER_READY " : "",
               (coordStatus & GT911_STATUS_LARGE_DETECT) ? "LARGE_DETECT " : "",
               (coordStatus & GT911_STATUS_HAVE_KEY)     ? "HAVE_KEY "     : "",
               (unsigned)(coordStatus & GT911_STATUS_POINT_COUNT_MASK),
               ((coordStatus & GT911_STATUS_POINT_COUNT_MASK) == 1u) ? "" : "s");
    }
    else
    {
        printf("    Coordinate Status: read failed\n\r");
    }

    terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    LCD_CTP_RESET_PIN (RJ9): %s\n\r", LCD_CTP_RESET_PIN ? "high (released)" : "low (in reset)");
    printf("    LCD_CTP_INT_PIN (RJ8) raw level: %s\n\r", LCD_CTP_INT_PIN ? "high" : "low");

    terminalTextAttributesReset();
}
