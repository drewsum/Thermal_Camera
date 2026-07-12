/*******************************************************************************
  SD Card File I/O Helpers

  File Name:
    sd_fileio.h

  Summary:
    Thin app-facing wrappers around FatFs (sdhc/fatfs/) so USB UART
    command handlers stay short -- the FatFs-flavored equivalent of what
    SST25VF080B_SelfTest() is for the SPI flash driver, but for file
    operations rather than raw device access.
*******************************************************************************/

#ifndef SD_FILEIO_H
#define SD_FILEIO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Mounts the FAT volume on the currently-initialized SD card (f_mount with
// opt=1 -- mounts immediately rather than lazily on first file access, so
// a bad/unformatted/absent card is reported right away instead of on the
// first real file operation). Requires SD_Card_Initialize() to have
// already succeeded.
bool SDFileIO_Mount(void);

// Unmounts the FAT volume and powers the card down via
// SD_Card_PowerDown(). Backing call for the "SD Eject" USB UART command.
bool SDFileIO_Unmount(void);

// Prints one line per directory entry via the caller-supplied
// `printLine` callback (so this stays terminal/formatting agnostic).
// `path` defaults to the root ("/") if NULL or empty.
bool SDFileIO_ListFiles(const char *path, void (*printLine)(const char *line));

// Opens `path` and streams its contents to the terminal via printf() in
// fixed-size chunks. Intended for text files -- no binary-safety handling
// (control/high-bit bytes are passed through printf() as-is).
bool SDFileIO_ReadTextFileToTerminal(const char *path);

// Creates (or truncates, if it already exists) `path` and writes `length`
// bytes from `data` to it in one shot.
bool SDFileIO_WriteFile(const char *path, const uint8_t *data, size_t length);

// Deletes `path`.
bool SDFileIO_DeleteFile(const char *path);

// Reports the mounted volume's FAT type ("FAT12"/"FAT16"/"FAT32"), label,
// and total/free space in KB. Any of the four output pointers may be
// NULL if that piece isn't needed. Returns false if no volume is
// mounted.
bool SDFileIO_GetVolumeInfo(char *fsTypeStr, size_t fsTypeStrSize,
        char *labelStr, size_t labelStrSize, uint32_t *totalKB, uint32_t *freeKB);

// Write / read-back / verify / delete round-trip on a throwaway test
// file, mirroring SST25VF080B_SelfTest()'s structure. Prints a colored
// pass/fail per step and returns true only if every step passes. Backing
// call for the "SD Self Test" USB UART command.
bool SDFileIO_SelfTest(void);

#ifdef __cplusplus
}
#endif

#endif /* SD_FILEIO_H */
