/*******************************************************************************
  SST25VF080B SPI NOR Flash Driver

  File Name:
    sst25vf080b.h

  Summary:
    Driver for the Microchip/SST SST25VF080B 8-Mbit SPI NOR flash, built on
    spi3.h. Datasheet: Microchip DS20005045D.

  Description:
    There is exactly one of these on this board (U-unknown, on SPI3), with
    chip select and write-protect on dedicated GPIOs (nFLASH_SPI_CS_PIN,
    nFLASH_SPI_WP_PIN -- pin_macros.h), so unlike the I2C device drivers
    this one takes no address/instance parameter.

    Block protection: per the datasheet (Table 4-3 / its Note 2), the
    STATUS register's BP3:BP0 bits reset to 1 at power-up, which protects
    the ENTIRE array against program/erase -- not just a portion of it, as
    the "block" naming might suggest. WRSR itself is gated by the WP# pin
    and BPL bit (Table 4-1): with WP# low and BPL=1 the STATUS register is
    hardware-locked. This driver owns nFLASH_SPI_WP_PIN and exposes the
    whole mechanism as SST25VF080B_WriteProtectSet():
      enable  = WRSR(BP3:0=1111, BPL=1) while WP# is high, THEN WP# low
                -- array protected and the protection itself locked;
      disable = WP# high (unlocks WRSR), then WRSR(0x00) -- array writable.
    SST25VF080B_Initialize() ends in the PROTECTED state (boot default),
    so nothing below will program or erase until WriteProtectSet(false).

    CRITICAL guard rationale: with BP bits set the part silently IGNORES
    program/erase instructions -- BUSY never asserts, so the BUSY-poll
    "succeeds" and a naive caller would report success while writing
    nothing. Write/EraseSector/EraseChip therefore check the driver's WP
    state first and fail fast when protected.

    Timing: program/erase completion is detected by polling the STATUS
    register's BUSY bit (the datasheet's recommended method), bounded by
    the datasheet's *_TIMEOUT_* constants below (its Table 7-1 max
    program/erase times) so a wedged part fails fast instead of hanging the
    caller forever -- mirroring i2c_master.c's I2C_TRANSACTION_TIMEOUT_US.

    Byte Program only clears bits (1->0); as with any NOR flash, a region
    must be erased (all bytes 0xFF) before (re)programming it with new
    data that isn't a strict subset of what's already there. This driver
    doesn't enforce that -- it's the caller's responsibility, same as on
    the part itself.

    Not implemented: AAI (Auto Address Increment) word programming. Plain
    Byte Program is simpler to get right and is fast enough for this
    driver's callers (self-test, small config blobs); revisit if bulk
    programming throughput ever becomes a real need.
*******************************************************************************/

#ifndef SST25VF080B_H
#define SST25VF080B_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Total array size and addressing, 3-byte/24-bit addresses 0x000000-0x0FFFFF
#define SST25VF080B_SIZE_BYTES          0x100000u   // 8 Mbit = 1MB
#define SST25VF080B_SECTOR_SIZE         0x1000u     // 4KB, smallest erase granularity
#define SST25VF080B_BLOCK_SIZE_32K      0x8000u
#define SST25VF080B_BLOCK_SIZE_64K      0x10000u

// STATUS register (RDSR) bit fields
#define SST25VF080B_STATUS_BUSY         0x01u   // 1 = program/erase in progress
#define SST25VF080B_STATUS_WEL          0x02u   // 1 = write enable latch set
#define SST25VF080B_STATUS_BP0          0x04u
#define SST25VF080B_STATUS_BP1          0x08u
#define SST25VF080B_STATUS_BP2          0x10u
#define SST25VF080B_STATUS_BP3          0x20u
#define SST25VF080B_STATUS_BP_MASK      0x3Cu   // BP3:BP0 -- block protection level
#define SST25VF080B_STATUS_AAI          0x40u   // 1 = AAI programming mode active
#define SST25VF080B_STATUS_BPL          0x80u   // 1 = BP3:BP0 locked read-only

// Enables SPI3 (via SPI3_Initialize()), confirms a device responds to the
// JEDEC Read-ID instruction as an SST25VF080B, and puts the part in the
// boot-default PROTECTED state (WriteProtectSet(true) -- see the file
// header). Returns false if any step fails.
bool SST25VF080B_Initialize(void);

// Hardware write protection (BP bits + BPL + the WP# GPIO -- see the file
// header for the exact sequencing). Enable leaves the array protected and
// the protection register hardware-locked; disable makes the full array
// writable. Both verify the resulting STATUS register and return false if
// the part didn't take the change. Callers above the raw driver should
// flush any write-back caches (Flash_Disk_Sync()) BEFORE enabling.
bool SST25VF080B_WriteProtectSet(bool enable);

// True while the driver holds the part write-protected. Cheap (shadow
// state, no SPI traffic) -- safe to call per-sector on I/O paths.
bool SST25VF080B_WriteProtectIsEnabled(void);

// Confirms the device responds to JEDEC Read-ID (9Fh) with the
// manufacturer/memory-type/capacity bytes documented for this part
// (0xBF/0x25/0x8E).
bool SST25VF080B_Verify(void);

// Reads the STATUS register (RDSR, 05h).
bool SST25VF080B_ReadStatus(uint8_t *status);

// Copies `length` bytes starting at `address` into `data` using the plain
// Read (03h) instruction. Requests that run past the end of the array are
// silently truncated to fit, matching this project's other bounds-checked
// helpers (e.g. ddr2Read()).
void SST25VF080B_Read(uint32_t address, uint8_t *data, size_t length);

// Programs `length` bytes starting at `address`, one Byte Program (02h)
// instruction (with its own Write Enable + BUSY poll) per byte -- see the
// caveat above about erasing first. Returns false (having programmed as
// many leading bytes as succeeded) if any byte times out waiting for BUSY
// to clear, or if the request runs past the end of the array.
bool SST25VF080B_Write(uint32_t address, const uint8_t *data, size_t length);

// Erases the 4KB sector containing `address` (rounds `address` down to the
// containing sector boundary) to all 0xFF. Returns false on a BUSY timeout.
bool SST25VF080B_EraseSector(uint32_t address);

// Erases the entire array to all 0xFF. Returns false on a BUSY timeout.
// WARNING: destructive -- see SST25VF080B_SelfTest()'s caveat for why the
// self-test does NOT call this.
bool SST25VF080B_EraseChip(void);

// Runs a destructive read/write/erase integrity test confined to the last
// 4KB sector of the array (0xFF000-0xFFFFF) -- not the full 8Mbit, unlike
// ddr2SelfTest()'s full-32MB sweep. DDR2 is volatile SRAM-class silicon
// with no meaningful write-endurance limit, so testing all of it on every
// boot is free; this NOR flash's Program/Erase cycling IS
// endurance-limited, so routinely full-chip-erasing it on every self-test
// run would measurably consume that budget over the product's life for no
// added coverage (one sector proves the same command/timing/polling path
// as any other). Prints a colored pass/fail per test and returns true only
// if all pass. WARNING: overwrites the last 4KB sector -- don't call once
// anything is storing data there.
bool SST25VF080B_SelfTest(void);

// Prints the device's identification, STATUS register (decoded), and size
// to the terminal.
void SST25VF080B_PrintStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* SST25VF080B_H */
