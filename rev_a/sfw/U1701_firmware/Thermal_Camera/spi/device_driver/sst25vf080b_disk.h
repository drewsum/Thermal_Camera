/*******************************************************************************
  SST25VF080B Virtual Block Disk Layer

  File Name:
    sst25vf080b_disk.h

  Summary:
    Presents the byte-addressed SST25VF080B NOR flash (sst25vf080b.h) as a
    512-byte-sector block device, so it can back both a FatFs volume
    (sdhc/fatfs/diskio.c, pdrv 1) and a USB mass storage LUN
    (usb/device_driver/usb_msd.c) through one coherent view.

  Description:
    NOR flash can only clear bits (1->0) when programming and only erases
    in 4KB sectors, but block-device consumers expect to rewrite any 512B
    sector at will. This layer bridges that with a single 4KB staging
    buffer: a write to a 512B sector pulls its containing 4KB erase page
    into RAM (flushing whatever page was staged before), modifies it in
    place, and marks it dirty; Flash_Disk_Sync() later erases the page and
    programs the staged contents back. Reads are served from the staging
    buffer when they hit the staged page (so a consumer always sees its
    own writes) and straight from the flash otherwise.

    Both consumers (FatFs and USB MSC) funnel through this one staging
    buffer, which is what keeps their views coherent -- but they must
    never be active at the same time; usb_msd.c's yield-to-host policy
    (unmount local FatFs volumes while a USB host owns the media)
    enforces that.

    The LAST 4KB erase sector of the array (0xFF000-0xFFFFF) is
    deliberately excluded from the disk: SST25VF080B_SelfTest()
    destructively exercises exactly that sector, so keeping it out of the
    volume means the boot/console self-test and the filesystem can never
    corrupt each other. Hence 2040 sectors (1020KB), not 2048.

    A dirty staged page means up to 4KB of data exists only in RAM until
    the next Flash_Disk_Sync() -- callers that care about durability
    (file close, USB eject/suspend, SCSI SYNCHRONIZE CACHE) must sync.
    diskio.c maps FatFs's CTRL_SYNC here, and usb_msd.c syncs on every
    flush trigger plus a write-idle timeout.
*******************************************************************************/

#ifndef SST25VF080B_DISK_H
#define SST25VF080B_DISK_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLASH_DISK_SECTOR_SIZE      512u

// One 4KB erase page = 8 virtual sectors
#define FLASH_DISK_SECTORS_PER_PAGE 8u

// Whole array minus the last 4KB erase sector (reserved for
// SST25VF080B_SelfTest() -- see file header) = 2040 sectors
#define FLASH_DISK_SECTOR_COUNT     2040u

// Marks the disk layer usable (SST25VF080B_Initialize() must already have
// succeeded -- this does not re-init the flash itself, matching how
// diskio.c's SD path assumes SDHC_Initialize() already ran) and resets the
// staging buffer to empty. Returns false if the flash doesn't answer JEDEC
// Read-ID, so a dead/absent part is caught here rather than as mysterious
// I/O errors later.
bool Flash_Disk_Initialize(void);

// True once Flash_Disk_Initialize() has succeeded.
bool Flash_Disk_IsInitialized(void);

// Number of 512B virtual sectors (FLASH_DISK_SECTOR_COUNT, as a function
// for the usb_msd.c per-LUN ops table's uniform signature).
uint32_t Flash_Disk_GetSectorCount(void);

// Reads `sectorCount` 512B sectors starting at `startSector`. Sectors that
// fall in the currently staged 4KB page are served from the staging buffer
// (which may hold newer-than-flash data); everything else reads straight
// from the flash. Returns false on out-of-range requests.
bool Flash_Disk_ReadSectors(uint32_t startSector, uint8_t *buffer, uint16_t sectorCount);

// Writes `sectorCount` 512B sectors starting at `startSector` into the
// staging buffer, faulting each target's 4KB page in (and flushing the
// previously staged page out) as needed. Data is NOT durable until the
// next Flash_Disk_Sync(). Returns false on out-of-range requests or if a
// flush of the previously staged page fails.
bool Flash_Disk_WriteSectors(uint32_t startSector, const uint8_t *buffer, uint16_t sectorCount);

// If the staged page is dirty: erases its 4KB flash sector and programs
// the staged contents back, clearing the dirty flag. No-op (true) when
// clean. Returns false on an erase/program failure or timeout.
bool Flash_Disk_Sync(void);

// True while the staging buffer holds data newer than the flash array --
// i.e. a Flash_Disk_Sync() would actually write something.
bool Flash_Disk_IsDirty(void);

// CP0 Count ticks (SYSCLK/2) since the most recent Flash_Disk_WriteSectors()
// call -- what usb_msd.c's write-idle flush compares against, so a burst
// of writes isn't interrupted by an eager flush after every sector.
uint32_t Flash_Disk_TicksSinceLastWrite(void);

// Prints the disk layer's state (geometry, staged page, dirty flag) to
// the terminal. The underlying part's own registers are
// SST25VF080B_PrintStatus()'s job, not repeated here.
void Flash_Disk_PrintStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* SST25VF080B_DISK_H */
