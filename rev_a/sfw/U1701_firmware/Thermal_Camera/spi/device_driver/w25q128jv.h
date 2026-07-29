/*******************************************************************************
  W25Q128JVSIQ SPI NOR Flash Driver

  File Name:
    w25q128jv.h

  Summary:
    Driver for the Winbond W25Q128JVSIQ 128-Mbit SPI NOR flash, built on
    spi3.h. Replaces the SST25VF080B (sst25vf080b.h, left in the tree
    unused) on the same SPI3 bus / nFLASH_SPI_CS_PIN / nFLASH_SPI_WP_PIN
    wiring -- see this project's schematic for the rev this footprint swap
    landed in.

  Description:
    There is exactly one of these on this board (on SPI3), with chip select
    and write-protect on dedicated GPIOs (nFLASH_SPI_CS_PIN,
    nFLASH_SPI_WP_PIN -- pin_macros.h), so unlike the I2C device drivers
    this one takes no address/instance parameter.

    Status register / write protection: unlike the SST25VF080B (whose
    BP3:BP0 reset to 1111 -- protected -- at power-up), this part's Status
    Register 1 resets to 0x00 (BP2:BP0 = 000, unprotected) per its
    datasheet. This driver does not rely on that power-on state: like the
    SST25VF080B driver, W25Q128JV_Initialize() unconditionally drives the
    part into a known PROTECTED state before returning, so the rest of the
    firmware (flash_fileio.c, diskio.c, usb_msd.c) can keep assuming "boot
    default: protected" regardless of which chip is fitted.

    W25Q128JV_WriteProtectSet() sets/clears BP2:BP0 (block protect, TB=0
    so BP2:BP0=111 protects the ENTIRE array, matching the SST driver's
    whole-array semantics) together with SRP0 (Status Register Protect 0,
    Status Register 1 bit 7): SRP0=1 with WP# driven low hardware-locks
    Status Register 1 against further WRSR (this part's equivalent of the
    SST25VF080B's BPL bit + WP# scheme):
      enable  = WRSR(BP2:0=111, SRP0=1) while WP# is high, THEN WP# low
                -- array protected and the protection itself locked;
      disable = WP# high (unlocks WRSR), then WRSR(0x00) -- array writable.
    W25Q128JV_Initialize() ends in the PROTECTED state (boot default), so
    nothing below will program or erase until WriteProtectSet(false).

    CRITICAL guard rationale: with BP bits set the part silently IGNORES
    program/erase instructions -- BUSY never asserts, so the BUSY-poll
    "succeeds" and a naive caller would report success while writing
    nothing. Write/EraseSector/EraseChip therefore check the driver's WP
    state first and fail fast when protected. Same as sst25vf080b.h.

    Timing: program/erase completion is detected by polling the Status
    Register 1 BUSY bit (the datasheet's recommended method), bounded by
    the datasheet's max program/erase times below. Unlike the SST driver's
    single-subtraction CP0 tick timeout (fine for its <=50ms worst case),
    this part's Chip Erase can take up to 200s -- far longer than the
    ~42.9s a 32-bit CP0 Count difference can represent at this device's
    SYSCLK/2 = 100MHz tick rate before wrapping. W25Q128JV_WaitWhileBusy()
    instead accumulates the elapsed ticks across repeated short deltas
    (each poll-to-poll gap is microseconds, far below the 32-bit wrap
    window) into a uint64_t running total, so multi-wrap timeouts are
    measured correctly.

    This part's Page Program (02h) instruction -- unlike the SST25VF080B's
    Byte Program, which is a true single-byte-per-instruction op -- natively
    accepts 1-256 bytes per instruction as long as they stay within one
    256-byte page (the datasheet: bytes beyond a page boundary wrap back to
    the start of the same page rather than continuing into the next one).
    W25Q128JV_Write() takes advantage of this and issues one Page Program
    instruction per page-aligned chunk instead of one per byte -- material
    for this driver's real workload (backing the whole SPI flash FAT
    volume via w25q128jv_disk.h) instead of just small config blobs.

    Byte Program only clears bits (1->0); as with any NOR flash, a region
    must be erased (all bytes 0xFF) before (re)programming it with new
    data that isn't a strict subset of what's already there. This driver
    doesn't enforce that -- it's the caller's responsibility, same as on
    the part itself.

    Only 4KB Sector Erase is implemented (what the disk layer needs to
    match its 4KB staging page); the part's 32KB/64KB Block Erase
    instructions aren't exposed, mirroring sst25vf080b.h leaving those
    granularities undriven too.

    This part supports Dual/Quad SPI (the "Q" in the part number) and a
    Quad Enable bit in Status Register 2, but SPI3 (spi3.c) is configured
    for plain single-bit SPI only -- this driver never touches Status
    Register 2 or QE, and only ever uses the standard single-bit Read/Page
    Program instructions (03h/02h), not their dual/quad-output variants.
*******************************************************************************/

#ifndef W25Q128JV_H
#define W25Q128JV_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Total array size and addressing, 3-byte/24-bit addresses 0x000000-0xFFFFFF
#define W25Q128JV_SIZE_BYTES          0x1000000u  // 128 Mbit = 16MB
#define W25Q128JV_SECTOR_SIZE         0x1000u     // 4KB, smallest erase granularity
#define W25Q128JV_PAGE_SIZE           0x100u      // 256B, Page Program granularity

// Status Register 1 (RDSR1, 05h) bit fields
#define W25Q128JV_STATUS_BUSY         0x01u   // 1 = program/erase in progress
#define W25Q128JV_STATUS_WEL          0x02u   // 1 = write enable latch set
#define W25Q128JV_STATUS_BP0          0x04u
#define W25Q128JV_STATUS_BP1          0x08u
#define W25Q128JV_STATUS_BP2          0x10u
#define W25Q128JV_STATUS_BP_MASK      0x1Cu   // BP2:BP0 -- block protection level
#define W25Q128JV_STATUS_TB           0x20u   // 1 = BP2:BP0 protect from the top of the array
#define W25Q128JV_STATUS_SEC          0x40u   // 1 = BP2:BP0 addresses 4KB sectors, not 64KB blocks
#define W25Q128JV_STATUS_SRP0         0x80u   // Status Register Protect 0 (locks SR1 with WP# low)

// Enables SPI3 (via SPI3_Initialize()), confirms a device responds to the
// JEDEC Read-ID instruction as a W25Q128JV, and puts the part in the
// boot-default PROTECTED state (WriteProtectSet(true) -- see the file
// header). Returns false if any step fails.
bool W25Q128JV_Initialize(void);

// Hardware write protection (BP bits + SRP0 + the WP# GPIO -- see the file
// header for the exact sequencing). Enable leaves the array protected and
// Status Register 1 hardware-locked; disable makes the full array
// writable. Both verify the resulting status register and return false if
// the part didn't take the change. Callers above the raw driver should
// flush any write-back caches (W25Q128JV_Disk_Sync()) BEFORE enabling.
bool W25Q128JV_WriteProtectSet(bool enable);

// True while the driver holds the part write-protected. Cheap (shadow
// state, no SPI traffic) -- safe to call per-sector on I/O paths.
bool W25Q128JV_WriteProtectIsEnabled(void);

// Confirms the device responds to JEDEC Read-ID (9Fh) with the
// manufacturer/memory-type/capacity bytes documented for this part
// (0xEF/0x40/0x18).
bool W25Q128JV_Verify(void);

// Reads Status Register 1 (RDSR1, 05h).
bool W25Q128JV_ReadStatus(uint8_t *status);

// Copies `length` bytes starting at `address` into `data` using the plain
// Read (03h) instruction. Requests that run past the end of the array are
// silently truncated to fit, matching this project's other bounds-checked
// helpers (e.g. ddr2Read()).
void W25Q128JV_Read(uint32_t address, uint8_t *data, size_t length);

// Programs `length` bytes starting at `address` via Page Program (02h),
// one instruction per page-aligned chunk (each chunk its own Write Enable
// + BUSY poll) -- see the file header for why this doesn't program
// byte-by-byte like sst25vf080b.c, and the caveat above about erasing
// first. Returns false (having programmed as many leading bytes as
// succeeded) if any chunk times out waiting for BUSY to clear, or if the
// request runs past the end of the array.
bool W25Q128JV_Write(uint32_t address, const uint8_t *data, size_t length);

// Erases the 4KB sector containing `address` (rounds `address` down to the
// containing sector boundary) to all 0xFF. Returns false on a BUSY timeout.
bool W25Q128JV_EraseSector(uint32_t address);

// Erases the entire array to all 0xFF. Returns false on a BUSY timeout.
// WARNING: destructive -- see W25Q128JV_SelfTest()'s caveat for why the
// self-test does NOT call this.
bool W25Q128JV_EraseChip(void);

// Runs a destructive read/write/erase integrity test confined to the last
// 4KB sector of the array (0xFFF000-0xFFFFFF) -- not the full 128Mbit,
// mirroring sst25vf080b.h's SST25VF080B_SelfTest() rationale: Program/
// Erase cycling is endurance-limited, so a full-chip-erase self-test on
// every boot would measurably consume that budget for no added coverage.
// Prints a colored pass/fail per test and returns true only if all pass.
// WARNING: overwrites the last 4KB sector -- don't call once anything is
// storing data there.
bool W25Q128JV_SelfTest(void);

// Prints the device's identification, Status Register 1 (decoded), and
// size to the terminal.
void W25Q128JV_PrintStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* W25Q128JV_H */
