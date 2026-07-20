/*******************************************************************************
  SST25VF080B SPI NOR Flash Driver

  File Name:
    sst25vf080b.c

  Summary:
    Driver for the Microchip/SST SST25VF080B 8-Mbit SPI NOR flash, built on
    spi3.h. See sst25vf080b.h for the block-protection and timing caveats.
*******************************************************************************/

#include "spi/device_driver/sst25vf080b.h"
#include "spi/spi3.h"
#include "core/device_control.h"
#include "gpio/pin_macros.h"
#include "usb_uart/terminal_control.h"
#include <xc.h>
#include <sys/attribs.h>

#include <stdio.h>

// *****************************************************************************
// Section: Instruction Set (datasheet Table 4-4)
// *****************************************************************************

#define SST25VF080B_CMD_READ               0x03u
#define SST25VF080B_CMD_FAST_READ          0x0Bu
#define SST25VF080B_CMD_SECTOR_ERASE_4K    0x20u
#define SST25VF080B_CMD_CHIP_ERASE         0x60u
#define SST25VF080B_CMD_BYTE_PROGRAM       0x02u
#define SST25VF080B_CMD_RDSR                0x05u
#define SST25VF080B_CMD_WRSR                0x01u
#define SST25VF080B_CMD_WREN                0x06u
#define SST25VF080B_CMD_JEDEC_ID            0x9Fu

// Expected JEDEC Read-ID (9Fh) response, datasheet Table 4-5
#define SST25VF080B_JEDEC_MANUFACTURER_ID   0xBFu   // Microchip/SST
#define SST25VF080B_JEDEC_MEMORY_TYPE       0x25u   // SPI Serial Flash
#define SST25VF080B_JEDEC_CAPACITY          0x8Eu   // SST25VF080B

// Max operation times, datasheet Table 7-1 (used as BUSY-poll timeout
// bounds, not fixed delays -- every operation still polls STATUS and
// returns as soon as BUSY clears, usually well under these worst cases).
#define SST25VF080B_TIMEOUT_TICKS(us)  ((uint32_t)(((uint64_t)SYSCLK_INT / 2u) * (us) / 1000000u))
#define SST25VF080B_TIMEOUT_BYTE_PROGRAM_TICKS   SST25VF080B_TIMEOUT_TICKS(10u)      // TBP  max = 10us
#define SST25VF080B_TIMEOUT_ERASE_TICKS          SST25VF080B_TIMEOUT_TICKS(25000u)   // TSE/TBE max = 25ms
#define SST25VF080B_TIMEOUT_CHIP_ERASE_TICKS     SST25VF080B_TIMEOUT_TICKS(50000u)   // TSCE max = 50ms

// *****************************************************************************
// Section: Bus/Command Helpers
// *****************************************************************************

static void SST25VF080B_Select(void)
{
    nFLASH_SPI_CS_PIN = LOW;
}

// CE# High Time (TCPH) is speced at a max of 100ns even at this part's
// fastest 66 MHz clock option (datasheet Table 7-1) -- the instructions
// executed by the C code between a deselect and the next select already
// take far longer than that at this CPU's clock speed, so no explicit
// delay is needed here.
static void SST25VF080B_Deselect(void)
{
    nFLASH_SPI_CS_PIN = HIGH;
}

// Writes a 24-bit address MSB first, per every addressed instruction in
// Table 4-4 ("Address bits [A23-A0]").
static void SST25VF080B_SendAddress(uint32_t address)
{
    SPI3_TransferByte((uint8_t)(address >> 16));
    SPI3_TransferByte((uint8_t)(address >> 8));
    SPI3_TransferByte((uint8_t)address);
}

bool SST25VF080B_ReadStatus(uint8_t *status)
{
    SST25VF080B_Select();
    SPI3_TransferByte(SST25VF080B_CMD_RDSR);
    *status = SPI3_TransferByte(0x00u);
    SST25VF080B_Deselect();

    return true;
}

// Polls STATUS until BUSY clears or `timeoutTicks` (CP0 Count ticks, see
// SST25VF080B_TIMEOUT_TICKS()) elapses. Returns false on timeout.
static bool SST25VF080B_WaitWhileBusy(uint32_t timeoutTicks)
{
    uint32_t start = _CP0_GET_COUNT();
    uint8_t status;

    do
    {
        SST25VF080B_ReadStatus(&status);

        if (!(status & SST25VF080B_STATUS_BUSY))
        {
            return true;
        }

    } while ((uint32_t)(_CP0_GET_COUNT() - start) < timeoutTicks);

    return false;
}

// Issues Write Enable (06h) -- required before every program/erase/WRSR
// instruction (datasheet section 4.4).
static void SST25VF080B_WriteEnable(void)
{
    SST25VF080B_Select();
    SPI3_TransferByte(SST25VF080B_CMD_WREN);
    SST25VF080B_Deselect();
}

// Shadow of the write-protect state (sst25vf080b.h file header). Starts
// true because the part itself powers up with BP3:BP0 = 1111.
static bool wp_enabled = true;

// Write Status Register (01h). Only honored while WP# is high or BPL=0
// (Table 4-1) -- callers sequence nFLASH_SPI_WP_PIN accordingly.
static bool SST25VF080B_WriteStatus(uint8_t value)
{
    SST25VF080B_WriteEnable();
    SST25VF080B_Select();
    SPI3_TransferByte(SST25VF080B_CMD_WRSR);
    SPI3_TransferByte(value);
    SST25VF080B_Deselect();

    return SST25VF080B_WaitWhileBusy(SST25VF080B_TIMEOUT_BYTE_PROGRAM_TICKS);
}

bool SST25VF080B_WriteProtectSet(bool enable)
{
    uint8_t status;

    // WRSR must happen while the status register is still writable, so
    // WP# goes (or stays) high first in both directions
    nFLASH_SPI_WP_PIN = HIGH;

    uint8_t target = enable ? (SST25VF080B_STATUS_BP_MASK | SST25VF080B_STATUS_BPL)
                            : 0x00u;

    if (!SST25VF080B_WriteStatus(target))
    {
        return false;
    }

    if (!SST25VF080B_ReadStatus(&status))
    {
        return false;
    }

    if ((status & (SST25VF080B_STATUS_BP_MASK | SST25VF080B_STATUS_BPL)) != target)
    {
        return false;   // part didn't take the change
    }

    if (enable)
    {
        // Hardware-lock the status register: with WP# low and BPL=1,
        // WRSR is ignored until WP# is raised again (Table 4-1)
        nFLASH_SPI_WP_PIN = LOW;
    }

    wp_enabled = enable;
    return true;
}

bool SST25VF080B_WriteProtectIsEnabled(void)
{
    return wp_enabled;
}

// *****************************************************************************
// Section: Interface Routines
// *****************************************************************************

bool SST25VF080B_Verify(void)
{
    uint8_t manufacturerId, memoryType, capacity;

    SST25VF080B_Select();
    SPI3_TransferByte(SST25VF080B_CMD_JEDEC_ID);
    manufacturerId = SPI3_TransferByte(0x00u);
    memoryType     = SPI3_TransferByte(0x00u);
    capacity       = SPI3_TransferByte(0x00u);
    SST25VF080B_Deselect();

    return (manufacturerId == SST25VF080B_JEDEC_MANUFACTURER_ID) &&
           (memoryType     == SST25VF080B_JEDEC_MEMORY_TYPE) &&
           (capacity       == SST25VF080B_JEDEC_CAPACITY);
}

bool SST25VF080B_Initialize(void)
{
    if (!SPI3_Initialize())
    {
        return false;
    }

    if (!SST25VF080B_Verify())
    {
        return false;
    }

    // Boot default: PROTECTED (sst25vf080b.h file header). The part
    // already powers up with BP3:0 = 1111; this additionally sets BPL and
    // drives WP# low so the protection itself is hardware-locked, and
    // verifies the part took it. "Flash Write Protect: Off" lifts it.
    return SST25VF080B_WriteProtectSet(true);
}

void SST25VF080B_Read(uint32_t address, uint8_t *data, size_t length)
{
    if (address >= SST25VF080B_SIZE_BYTES)
    {
        return;
    }

    if (length > (SST25VF080B_SIZE_BYTES - address))
    {
        length = SST25VF080B_SIZE_BYTES - address;
    }

    SST25VF080B_Select();
    SPI3_TransferByte(SST25VF080B_CMD_FAST_READ);
    SST25VF080B_SendAddress(address);
    SPI3_TransferByte(0x00u);  // Fast Read dummy byte (datasheet Table 4-4)
    SPI3_TransferBlock(NULL, data, length);
    SST25VF080B_Deselect();
}

bool SST25VF080B_Write(uint32_t address, const uint8_t *data, size_t length)
{
    size_t i;

    // With BP bits set the part silently ignores Byte Program (BUSY never
    // asserts, so the poll below would "pass" without writing anything) --
    // fail honestly instead. Same guard on both erase functions.
    if (wp_enabled)
    {
        return false;
    }

    if (address >= SST25VF080B_SIZE_BYTES)
    {
        return false;
    }

    if (length > (SST25VF080B_SIZE_BYTES - address))
    {
        length = SST25VF080B_SIZE_BYTES - address;
    }

    for (i = 0; i < length; i++)
    {
        SST25VF080B_WriteEnable();

        SST25VF080B_Select();
        SPI3_TransferByte(SST25VF080B_CMD_BYTE_PROGRAM);
        SST25VF080B_SendAddress(address + i);
        SPI3_TransferByte(data[i]);
        SST25VF080B_Deselect();

        if (!SST25VF080B_WaitWhileBusy(SST25VF080B_TIMEOUT_BYTE_PROGRAM_TICKS))
        {
            return false;
        }
    }

    return true;
}

bool SST25VF080B_EraseSector(uint32_t address)
{
    uint32_t sectorAddress = address & ~(SST25VF080B_SECTOR_SIZE - 1u);

    if (wp_enabled)
    {
        return false;   // see SST25VF080B_Write()
    }

    SST25VF080B_WriteEnable();

    SST25VF080B_Select();
    SPI3_TransferByte(SST25VF080B_CMD_SECTOR_ERASE_4K);
    SST25VF080B_SendAddress(sectorAddress);
    SST25VF080B_Deselect();

    return SST25VF080B_WaitWhileBusy(SST25VF080B_TIMEOUT_ERASE_TICKS);
}

bool SST25VF080B_EraseChip(void)
{
    if (wp_enabled)
    {
        return false;   // see SST25VF080B_Write()
    }

    SST25VF080B_WriteEnable();

    SST25VF080B_Select();
    SPI3_TransferByte(SST25VF080B_CMD_CHIP_ERASE);
    SST25VF080B_Deselect();

    return SST25VF080B_WaitWhileBusy(SST25VF080B_TIMEOUT_CHIP_ERASE_TICKS);
}

// The last 4KB sector -- chosen so a self-test never collides with data
// starting from address 0, which is where any future real use of this
// flash will most naturally begin.
#define SST25VF080B_SELFTEST_SECTOR_ADDRESS  (SST25VF080B_SIZE_BYTES - SST25VF080B_SECTOR_SIZE)

bool SST25VF080B_SelfTest(void)
{
    static uint8_t writeBuffer[SST25VF080B_SECTOR_SIZE];
    static uint8_t readBuffer[SST25VF080B_SECTOR_SIZE];
    bool overallPass = true;
    uint32_t i;

    terminalTextAttributesReset();
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("SST25VF080B SPI Flash Self-Test:\r\n");

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
           (unsigned)SST25VF080B_SELFTEST_SECTOR_ADDRESS,
           (unsigned)(SST25VF080B_SIZE_BYTES - 1u));

    // ---- JEDEC ID ----
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    [1/4] JEDEC Read-ID: ");
    if (SST25VF080B_Verify())
    {
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("PASS\r\n");
    }
    else
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("FAIL (device did not identify as SST25VF080B)\r\n");
        overallPass = false;
    }

    // ---- Sector erase + blank check ----
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    [2/4] Sector erase: ");
    if (SST25VF080B_EraseSector(SST25VF080B_SELFTEST_SECTOR_ADDRESS))
    {
        bool blank = true;

        SST25VF080B_Read(SST25VF080B_SELFTEST_SECTOR_ADDRESS, readBuffer, sizeof(readBuffer));
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
    printf("    [3/4] Byte Program (%u bytes): ", (unsigned)sizeof(writeBuffer));
    for (i = 0; i < sizeof(writeBuffer); i++)
    {
        writeBuffer[i] = (uint8_t)(i ^ 0xA5u);
    }

    if (SST25VF080B_Write(SST25VF080B_SELFTEST_SECTOR_ADDRESS, writeBuffer, sizeof(writeBuffer)))
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
    SST25VF080B_Read(SST25VF080B_SELFTEST_SECTOR_ADDRESS, readBuffer, sizeof(readBuffer));

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
    SST25VF080B_EraseSector(SST25VF080B_SELFTEST_SECTOR_ADDRESS);

    if (overallPass) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    Overall: %s\r\n", overallPass ? "PASS" : "FAIL");
    terminalTextAttributesReset();

    return overallPass;
}

void SST25VF080B_PrintStatus(void)
{
    uint8_t status;
    bool identified = SST25VF080B_Verify();

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    --- SST25VF080B (SPI3) ---\n\r");

    terminalTextAttributes(identified ? GREEN_COLOR : RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    JEDEC ID: %s\n\r", identified ? "recognized (0xBF/0x25/0x8E)" : "unrecognized");

    if (!SST25VF080B_ReadStatus(&status))
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
    printf("    STATUS register: 0x%02X\n\r", status);

    if (status & SST25VF080B_STATUS_BUSY) terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("        BUSY: %s\n\r", (status & SST25VF080B_STATUS_BUSY) ? "program/erase in progress" : "ready");

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("        WEL:  %s\n\r", (status & SST25VF080B_STATUS_WEL) ? "write enabled" : "write disabled");

    if ((status & SST25VF080B_STATUS_BP_MASK) == 0) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("        Block protection level (BP3:BP0): 0x%X%s\n\r",
           (status & SST25VF080B_STATUS_BP_MASK) >> 2,
           (status & SST25VF080B_STATUS_BP_MASK) ? "" : " (none -- full array writable)");

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("        AAI:  %s\n\r", (status & SST25VF080B_STATUS_AAI) ? "AAI programming mode" : "Byte-Program mode");
    printf("        BPL:  %s\n\r", (status & SST25VF080B_STATUS_BPL) ? "BP3:BP0 locked read-only" : "BP3:BP0 writable");

    printf("    Size: %u bytes (%u x 4KB sectors)\n\r",
           (unsigned)SST25VF080B_SIZE_BYTES, (unsigned)(SST25VF080B_SIZE_BYTES / SST25VF080B_SECTOR_SIZE));

    terminalTextAttributesReset();
}
