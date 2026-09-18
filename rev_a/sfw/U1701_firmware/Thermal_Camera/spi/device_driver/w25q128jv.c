/*******************************************************************************
  W25Q128JVSIQ SPI NOR Flash Driver

  File Name:
    w25q128jv.c

  Summary:
    Driver for the Winbond W25Q128JVSIQ 128-Mbit SPI NOR flash, built on
    spi3.h. See w25q128jv.h for the block-protection, timing, and Page
    Program batching notes.
*******************************************************************************/

#include "spi/device_driver/w25q128jv.h"
#include "spi/spi3.h"
#include "core/device_control.h"
#include "core/watchdog_timer.h"
#include "gpio/pin_macros.h"
#include "usb_uart/terminal_control.h"
#include <xc.h>
#include <sys/attribs.h>

#include <stdio.h>

// *****************************************************************************
// Section: Instruction Set
// *****************************************************************************

#define W25Q128JV_CMD_READ               0x03u
#define W25Q128JV_CMD_SECTOR_ERASE_4K    0x20u
#define W25Q128JV_CMD_CHIP_ERASE         0xC7u
#define W25Q128JV_CMD_PAGE_PROGRAM       0x02u
#define W25Q128JV_CMD_RDSR1              0x05u
#define W25Q128JV_CMD_WRSR1              0x01u
#define W25Q128JV_CMD_WREN               0x06u
#define W25Q128JV_CMD_JEDEC_ID           0x9Fu

// Expected JEDEC Read-ID (9Fh) response
#define W25Q128JV_JEDEC_MANUFACTURER_ID   0xEFu   // Winbond
#define W25Q128JV_JEDEC_MEMORY_TYPE       0x40u   // SPI Serial Flash (Q series)
#define W25Q128JV_JEDEC_CAPACITY          0x18u   // W25Q128JV, 128 Mbit

// Max operation times (used as BUSY-poll timeout bounds, not fixed delays --
// every operation still polls the status register and returns as soon as
// BUSY clears, usually well under these worst cases). Unlike
// sst25vf080b.c's TIMEOUT_TICKS (which fits in a uint32_t for its <=50ms
// worst case), this part's 200s Chip Erase timeout requires uint64_t --
// see the file header's note on W25Q128JV_WaitWhileBusy()'s accumulation.
#define W25Q128JV_TIMEOUT_TICKS(us)  ((uint64_t)(((uint64_t)SYSCLK_INT / 2u) * (us) / 1000000u))
#define W25Q128JV_TIMEOUT_PAGE_PROGRAM_TICKS   W25Q128JV_TIMEOUT_TICKS(3000ull)        // tPP max = 3ms
#define W25Q128JV_TIMEOUT_SECTOR_ERASE_TICKS   W25Q128JV_TIMEOUT_TICKS(400000ull)      // tSE max = 400ms
#define W25Q128JV_TIMEOUT_CHIP_ERASE_TICKS     W25Q128JV_TIMEOUT_TICKS(200000000ull)   // tCE max = 200s
#define W25Q128JV_TIMEOUT_WRSR_TICKS           W25Q128JV_TIMEOUT_TICKS(15000ull)       // tW  max = 15ms

// *****************************************************************************
// Section: Bus/Command Helpers
// *****************************************************************************

static void W25Q128JV_Select(void)
{
    nFLASH_SPI_CS_PIN = LOW;
}

// CS# High Time is speced at a few tens of ns even at this part's fastest
// clock options -- the instructions executed by the C code between a
// deselect and the next select already take far longer than that at this
// CPU's clock speed, so no explicit delay is needed here (same rationale
// as sst25vf080b.c's Deselect()).
static void W25Q128JV_Deselect(void)
{
    nFLASH_SPI_CS_PIN = HIGH;
}

// Writes a 24-bit address MSB first, per every addressed instruction.
static void W25Q128JV_SendAddress(uint32_t address)
{
    SPI3_TransferByte((uint8_t)(address >> 16));
    SPI3_TransferByte((uint8_t)(address >> 8));
    SPI3_TransferByte((uint8_t)address);
}

bool W25Q128JV_ReadStatus(uint8_t *status)
{
    W25Q128JV_Select();
    SPI3_TransferByte(W25Q128JV_CMD_RDSR1);
    *status = SPI3_TransferByte(0x00u);
    W25Q128JV_Deselect();

    return true;
}

// Polls Status Register 1 until BUSY clears or `timeoutTicks` (CP0 Count
// ticks, see W25Q128JV_TIMEOUT_TICKS()) elapses. Returns false on timeout.
//
// Accumulates elapsed ticks across poll-to-poll deltas into a uint64_t
// running total rather than doing one subtraction against a fixed start
// point -- a Chip Erase timeout (up to 200s) far exceeds the ~42.9s a
// 32-bit CP0 Count difference can represent at this device's SYSCLK/2 =
// 100MHz tick rate before wrapping, but each individual delta here (one
// RDSR1 round-trip) is microseconds, nowhere near that wrap window, so the
// accumulated total stays correct across any number of wraps.
//
// Kicked every iteration: the WDT timeout (2.048s, watchdog_timer.h) is far
// shorter than Chip Erase's worst case (200s), and each iteration already
// costs a full RDSR1 SPI round-trip, so kickTheDog()'s overhead here is
// immaterial. Without this, "Flash Format" reset the MCU mid-erase every
// time -- the loop's own timeout never got a chance to fire first.
static bool W25Q128JV_WaitWhileBusy(uint64_t timeoutTicks)
{
    uint32_t last = _CP0_GET_COUNT();
    uint64_t elapsed = 0;
    uint8_t status;

    for (;;)
    {
        kickTheDog();
        W25Q128JV_ReadStatus(&status);

        if (!(status & W25Q128JV_STATUS_BUSY))
        {
            return true;
        }

        uint32_t now = _CP0_GET_COUNT();
        elapsed += (uint32_t)(now - last);
        last = now;

        if (elapsed >= timeoutTicks)
        {
            return false;
        }
    }
}

// Issues Write Enable (06h) -- required before every program/erase/WRSR
// instruction.
static void W25Q128JV_WriteEnable(void)
{
    W25Q128JV_Select();
    SPI3_TransferByte(W25Q128JV_CMD_WREN);
    W25Q128JV_Deselect();
}

// Shadow of the write-protect state (w25q128jv.h file header). Starts true
// so a driver bug that skips Initialize() fails safe (refuses writes)
// rather than defaulting to writable -- the part's own power-on state is
// actually unprotected (BP2:0=000), unlike the SST25VF080B, but
// Initialize() unconditionally forces PROTECTED before this flag is ever
// read for real.
static bool wp_enabled = true;

// Write Status Register 1 (01h, single data byte so only SR1 is written,
// not SR2). Only honored while WP# is high or SRP0=0 -- callers sequence
// nFLASH_SPI_WP_PIN accordingly.
static bool W25Q128JV_WriteStatus(uint8_t value)
{
    W25Q128JV_WriteEnable();
    W25Q128JV_Select();
    SPI3_TransferByte(W25Q128JV_CMD_WRSR1);
    SPI3_TransferByte(value);
    W25Q128JV_Deselect();

    return W25Q128JV_WaitWhileBusy(W25Q128JV_TIMEOUT_WRSR_TICKS);
}

bool W25Q128JV_WriteProtectSet(bool enable)
{
    uint8_t status;

    // WRSR must happen while the status register is still writable, so
    // WP# goes (or stays) high first in both directions
    nFLASH_SPI_WP_PIN = HIGH;

    // TB=0, BP2:BP0=111 protects the entire array (not just a portion of
    // it, despite the "block" naming) -- same whole-array semantics as
    // sst25vf080b.c's SST25VF080B_WriteProtectSet(). SRP0=1 is this part's
    // equivalent of the SST driver's BPL bit.
    uint8_t target = enable ? (W25Q128JV_STATUS_BP_MASK | W25Q128JV_STATUS_SRP0)
                            : 0x00u;

    if (!W25Q128JV_WriteStatus(target))
    {
        return false;
    }

    if (!W25Q128JV_ReadStatus(&status))
    {
        return false;
    }

    if ((status & (W25Q128JV_STATUS_BP_MASK | W25Q128JV_STATUS_TB | W25Q128JV_STATUS_SRP0)) != target)
    {
        return false;   // part didn't take the change
    }

    if (enable)
    {
        // Hardware-lock Status Register 1: with WP# low and SRP0=1, WRSR
        // is ignored until WP# is raised again
        nFLASH_SPI_WP_PIN = LOW;
    }

    wp_enabled = enable;
    return true;
}

bool W25Q128JV_WriteProtectIsEnabled(void)
{
    return wp_enabled;
}

// *****************************************************************************
// Section: Interface Routines
// *****************************************************************************

bool W25Q128JV_Verify(void)
{
    uint8_t manufacturerId, memoryType, capacity;

    W25Q128JV_Select();
    SPI3_TransferByte(W25Q128JV_CMD_JEDEC_ID);
    manufacturerId = SPI3_TransferByte(0x00u);
    memoryType     = SPI3_TransferByte(0x00u);
    capacity       = SPI3_TransferByte(0x00u);
    W25Q128JV_Deselect();

    return (manufacturerId == W25Q128JV_JEDEC_MANUFACTURER_ID) &&
           (memoryType     == W25Q128JV_JEDEC_MEMORY_TYPE) &&
           (capacity       == W25Q128JV_JEDEC_CAPACITY);
}

bool W25Q128JV_Initialize(void)
{
    if (!SPI3_Initialize())
    {
        return false;
    }

    if (!W25Q128JV_Verify())
    {
        return false;
    }

    // Boot default: PROTECTED (w25q128jv.h file header) -- forced
    // explicitly rather than relied upon, since this part's own power-on
    // state is unprotected. "Flash Write Protect: Off" lifts it.
    return W25Q128JV_WriteProtectSet(true);
}

void W25Q128JV_Read(uint32_t address, uint8_t *data, size_t length)
{
    if (address >= W25Q128JV_SIZE_BYTES)
    {
        return;
    }

    if (length > (W25Q128JV_SIZE_BYTES - address))
    {
        length = W25Q128JV_SIZE_BYTES - address;
    }

    W25Q128JV_Select();
    SPI3_TransferByte(W25Q128JV_CMD_READ);
    W25Q128JV_SendAddress(address);
    SPI3_TransferBlock(NULL, data, length);
    W25Q128JV_Deselect();
}

bool W25Q128JV_Write(uint32_t address, const uint8_t *data, size_t length)
{
    size_t written;

    // With BP bits set the part silently ignores Page Program (BUSY never
    // asserts, so the poll below would "pass" without writing anything) --
    // fail honestly instead. Same guard on both erase functions.
    if (wp_enabled)
    {
        return false;
    }

    if (address >= W25Q128JV_SIZE_BYTES)
    {
        return false;
    }

    if (length > (W25Q128JV_SIZE_BYTES - address))
    {
        length = W25Q128JV_SIZE_BYTES - address;
    }

    written = 0;
    while (written < length)
    {
        uint32_t chunkAddress = address + written;
        uint32_t pageOffset = chunkAddress & (W25Q128JV_PAGE_SIZE - 1u);
        size_t chunkLength = W25Q128JV_PAGE_SIZE - pageOffset;

        if (chunkLength > (length - written))
        {
            chunkLength = length - written;
        }

        W25Q128JV_WriteEnable();

        W25Q128JV_Select();
        SPI3_TransferByte(W25Q128JV_CMD_PAGE_PROGRAM);
        W25Q128JV_SendAddress(chunkAddress);
        SPI3_TransferBlock(&data[written], NULL, chunkLength);
        W25Q128JV_Deselect();

        if (!W25Q128JV_WaitWhileBusy(W25Q128JV_TIMEOUT_PAGE_PROGRAM_TICKS))
        {
            return false;
        }

        written += chunkLength;
    }

    return true;
}

bool W25Q128JV_EraseSector(uint32_t address)
{
    uint32_t sectorAddress = address & ~(W25Q128JV_SECTOR_SIZE - 1u);

    if (wp_enabled)
    {
        return false;   // see W25Q128JV_Write()
    }

    W25Q128JV_WriteEnable();

    W25Q128JV_Select();
    SPI3_TransferByte(W25Q128JV_CMD_SECTOR_ERASE_4K);
    W25Q128JV_SendAddress(sectorAddress);
    W25Q128JV_Deselect();

    return W25Q128JV_WaitWhileBusy(W25Q128JV_TIMEOUT_SECTOR_ERASE_TICKS);
}

bool W25Q128JV_EraseChip(void)
{
    if (wp_enabled)
    {
        return false;   // see W25Q128JV_Write()
    }

    W25Q128JV_WriteEnable();

    W25Q128JV_Select();
    SPI3_TransferByte(W25Q128JV_CMD_CHIP_ERASE);
    W25Q128JV_Deselect();

    return W25Q128JV_WaitWhileBusy(W25Q128JV_TIMEOUT_CHIP_ERASE_TICKS);
}

// The last 4KB sector -- chosen so a self-test never collides with data
// starting from address 0, which is where any future real use of this
// flash will most naturally begin.
#define W25Q128JV_SELFTEST_SECTOR_ADDRESS  (W25Q128JV_SIZE_BYTES - W25Q128JV_SECTOR_SIZE)

bool W25Q128JV_SelfTest(void)
{
    static uint8_t writeBuffer[W25Q128JV_SECTOR_SIZE];
    static uint8_t readBuffer[W25Q128JV_SECTOR_SIZE];
    bool overallPass = true;
    uint32_t i;

    terminalTextAttributesReset();
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("W25Q128JV SPI Flash Self-Test:\r\n");

    if (wp_enabled)
    {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    Write protect is enabled -- run \"Flash Write Protect: Off\" first\r\n");
        terminalTextAttributesReset();
        return false;
    }

    terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    WARNING: this overwrites the last 4KB sector (0x%06X-0x%06X).\r\n"
           "    Do not run if anything is storing data there.\r\n",
           (unsigned)W25Q128JV_SELFTEST_SECTOR_ADDRESS,
           (unsigned)(W25Q128JV_SIZE_BYTES - 1u));

    // ---- JEDEC ID ----
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    [1/4] JEDEC Read-ID: ");
    if (W25Q128JV_Verify())
    {
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("PASS\r\n");
    }
    else
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("FAIL (device did not identify as W25Q128JV)\r\n");
        overallPass = false;
    }

    // ---- Sector erase + blank check ----
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    [2/4] Sector erase: ");
    if (W25Q128JV_EraseSector(W25Q128JV_SELFTEST_SECTOR_ADDRESS))
    {
        bool blank = true;

        W25Q128JV_Read(W25Q128JV_SELFTEST_SECTOR_ADDRESS, readBuffer, sizeof(readBuffer));
        for (i = 0; i < sizeof(readBuffer); i++)
        {
            if (readBuffer[i] != 0xFFu)
            {
                blank = false;
                break;
            }
        }

        if (blank)
        {
            terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("PASS\r\n");
        }
        else
        {
            terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("FAIL (byte %u read 0x%02X after erase, expected 0xFF)\r\n",
                   (unsigned)i, readBuffer[i]);
            overallPass = false;
        }
    }
    else
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("FAIL (timed out waiting for BUSY to clear)\r\n");
        overallPass = false;
    }

    // ---- Program a pattern, then read it back ----
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    [3/4] Page Program (%u bytes): ", (unsigned)sizeof(writeBuffer));
    for (i = 0; i < sizeof(writeBuffer); i++)
    {
        writeBuffer[i] = (uint8_t)(i ^ 0xA5u);
    }

    if (W25Q128JV_Write(W25Q128JV_SELFTEST_SECTOR_ADDRESS, writeBuffer, sizeof(writeBuffer)))
    {
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("PASS\r\n");
    }
    else
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("FAIL (timed out waiting for BUSY to clear)\r\n");
        overallPass = false;
    }

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    [4/4] Readback verify: ");
    W25Q128JV_Read(W25Q128JV_SELFTEST_SECTOR_ADDRESS, readBuffer, sizeof(readBuffer));

    {
        bool match = true;

        for (i = 0; i < sizeof(readBuffer); i++)
        {
            if (readBuffer[i] != writeBuffer[i])
            {
                match = false;
                break;
            }
        }

        if (match)
        {
            terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("PASS\r\n");
        }
        else
        {
            terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("FAIL at byte %u: wrote 0x%02X, read 0x%02X\r\n",
                   (unsigned)i, writeBuffer[i], readBuffer[i]);
            overallPass = false;
        }
    }

    // Leave the test sector erased rather than full of test-pattern bytes
    W25Q128JV_EraseSector(W25Q128JV_SELFTEST_SECTOR_ADDRESS);

    if (overallPass) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    Overall: %s\r\n", overallPass ? "PASS" : "FAIL");
    terminalTextAttributesReset();

    return overallPass;
}

void W25Q128JV_PrintStatus(void)
{
    uint8_t status;
    bool identified = W25Q128JV_Verify();

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    --- W25Q128JV (SPI3) ---\n\r");

    terminalTextAttributes(identified ? GREEN_COLOR : RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    JEDEC ID: %s\n\r", identified ? "recognized (0xEF/0x40/0x18)" : "unrecognized");

    if (!W25Q128JV_ReadStatus(&status))
    {
        terminalTextAttributesReset();
        return;
    }

    if (wp_enabled) terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Write Protect: %s (WP# pin %s)\n\r",
           wp_enabled ? "ENABLED" : "disabled",
           nFLASH_SPI_WP_PIN ? "high" : "low/asserted");

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    STATUS register 1: 0x%02X\n\r", status);

    if (status & W25Q128JV_STATUS_BUSY) terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("        BUSY: %s\n\r", (status & W25Q128JV_STATUS_BUSY) ? "program/erase in progress" : "ready");

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("        WEL:  %s\n\r", (status & W25Q128JV_STATUS_WEL) ? "write enabled" : "write disabled");

    if ((status & W25Q128JV_STATUS_BP_MASK) == 0) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("        Block protection level (BP2:BP0): 0x%X%s\n\r",
           (status & W25Q128JV_STATUS_BP_MASK) >> 2,
           (status & W25Q128JV_STATUS_BP_MASK) ? "" : " (none -- full array writable)");

    printf("        TB:   %s\n\r", (status & W25Q128JV_STATUS_TB) ? "protect from top" : "protect from bottom");
    printf("        SEC:  %s\n\r", (status & W25Q128JV_STATUS_SEC) ? "4KB sector addressing" : "64KB block addressing");
    printf("        SRP0: %s\n\r", (status & W25Q128JV_STATUS_SRP0) ? "Status Register locked (with WP# low)" : "Status Register writable");

    printf("    Size: %u bytes (%u x 4KB sectors)\n\r",
           (unsigned)W25Q128JV_SIZE_BYTES, (unsigned)(W25Q128JV_SIZE_BYTES / W25Q128JV_SECTOR_SIZE));

    terminalTextAttributesReset();
}
