/*******************************************************************************
  SPI Flash File I/O Helpers

  File Name:
    flash_fileio.h

  Summary:
    Thin app-facing FatFs wrappers for the SPI flash volume ("1:") -- the
    peer of sdhc/sd_fileio.h, which plays the same role for the SD card
    volume ("0:", FatFs's default drive).

  Description:
    The SST25VF080B is exposed to FatFs as physical drive 1 through
    sdhc/fatfs/diskio.c -> sst25vf080b_disk.h (FF_VOLUMES is 2 in
    ffconf.h). Unlike the SD card, the flash is soldered down and always
    present, so "no filesystem yet" just means a fresh/erased part --
    FlashFileIO_MountAndFormatIfNeeded() handles that by f_mkfs()ing a
    FAT volume on first boot rather than reporting an error.

    Every path passed to FatFs from here carries the explicit "1:" drive
    prefix, which is what keeps this module and sd_fileio.c (prefix-less
    paths -> default drive 0) out of each other's volumes.
*******************************************************************************/

#ifndef FLASH_FILEIO_H
#define FLASH_FILEIO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Volume label written by FlashFileIO_MountAndFormatIfNeeded() /
// FlashFileIO_Format() -- what the volume shows up as when a USB host
// mounts it (FAT labels max out at 11 chars; the full display name comes
// from autorun.inf, see flash_fileio.c)
#define FLASH_FILEIO_VOLUME_LABEL   "THERMAL SPI"

// Mounts the FAT volume on the SPI flash (f_mount with opt=1, immediate --
// same rationale as SDFileIO_Mount()). If no valid FAT volume exists
// (fresh or erased part), formats one first: FAT/SFD ("superfloppy", no
// partition table -- what both FatFs and USB hosts expect on small
// media), then labels it FLASH_FILEIO_VOLUME_LABEL. An existing volume's
// label is set only if currently blank, so a deliberately renamed volume
// stays renamed. Requires Flash_Disk_Initialize() to have already
// succeeded.
bool FlashFileIO_MountAndFormatIfNeeded(void);

// Unmounts the FAT volume, flushing the disk layer's staging buffer
// (Flash_Disk_Sync()) so nothing is left RAM-only. Backing call for the
// USB yield-to-host handoff.
bool FlashFileIO_Unmount(void);

// True while the flash volume is mounted for local (firmware) use.
bool FlashFileIO_IsMounted(void);

// Unconditionally re-formats the flash volume (f_mkfs + relabel +
// remount), destroying its contents. Backing call for the
// "Flash FS Format" USB UART command -- and the recovery step after
// "Erase SPI Flash" wipes the FAT structures.
bool FlashFileIO_Format(void);

// Prints one line per directory entry via the caller-supplied `printLine`
// callback -- mirrors SDFileIO_ListFiles(). `path` defaults to the volume
// root if NULL or empty; relative paths get the "1:" prefix applied here.
bool FlashFileIO_ListFiles(const char *path, void (*printLine)(const char *line));

// Dumps a text file's contents to the terminal via printf -- mirrors
// SDFileIO_ReadTextFileToTerminal(). Relative paths get the "1:" prefix
// applied here. Backing call for the "Flash Read File:" USB UART command.
bool FlashFileIO_ReadTextFileToTerminal(const char *path);

// Reports the mounted volume's FAT type, label, and total/free space in
// KB -- mirrors SDFileIO_GetVolumeInfo(). Any output pointer may be NULL.
bool FlashFileIO_GetVolumeInfo(char *fsTypeStr, size_t fsTypeStrSize,
        char *labelStr, size_t labelStrSize, uint32_t *totalKB, uint32_t *freeKB);

// Write / read-back / verify / delete round-trip on a throwaway test file,
// mirroring SDFileIO_SelfTest(). Prints a colored pass/fail per step and
// returns true only if every step passes. Backing call for the
// "Flash FS Self Test" USB UART command.
bool FlashFileIO_SelfTest(void);

#ifdef __cplusplus
}
#endif

#endif /* FLASH_FILEIO_H */
