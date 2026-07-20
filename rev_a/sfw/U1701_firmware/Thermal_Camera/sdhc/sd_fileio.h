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

// Unmounts the FAT volume and invalidates the cached card state via
// SD_Card_Deinitialize() -- card power is deliberately left ON, since
// card-detect sensing runs off the switched rail (see
// SD_Card_PowerDown()). Backing call for the "SD Eject" USB UART command
// and the hot-swap removal path.
bool SDFileIO_Unmount(void);

// Unmounts the FAT volume WITHOUT powering the card down -- the USB
// yield-to-host handoff (usb_msd.c): the firmware releases its FatFs view
// of the card, but the USB host is about to read the very same card, so
// it must stay powered and initialized.
bool SDFileIO_UnmountKeepPower(void);

// Applies the volume label "SD" if the mounted volume's label is
// currently blank (a card the user labeled themselves is never
// re-labeled). This is what names the drive when a USB host mounts the
// card. Call after a successful SDFileIO_Mount().
bool SDFileIO_EnsureLabel(void);

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
// and total/free space in both KB and exact bytes (totalBytes/freeBytes
// are computed directly from the sector count, not derived from the
// already-rounded KB values, so they're byte-exact). Any of the six
// output pointers may be NULL if that piece isn't needed. Returns false
// if no volume is mounted.
bool SDFileIO_GetVolumeInfo(char *fsTypeStr, size_t fsTypeStrSize,
        char *labelStr, size_t labelStrSize, uint32_t *totalKB, uint32_t *freeKB,
        uint32_t *totalBytes, uint32_t *freeBytes);

// Main-loop service for SD card hot-swap: consumes sd_card_hotswap_event
// (sd_card.h -- latched by the Port A change-notice ISR on any card-detect
// edge), debounces via SD_Card_IsPresent(), and converges actual state to
// detected state: fresh insertion -> SD_Card_Initialize() + mount + label,
// removal -> unmount + power-down. No-ops (cheap flag check) when no edge
// has fired. Deliberately stands down while usb_msd_media_owned_by_host --
// the USB MSC layer runs its own presence tracking + UNIT ATTENTION
// reporting for the host (usb_msd.c msdLunMediaReady()) and remounts the
// firmware view itself when the host releases the media.
void SDFileIO_HotSwapTasks(void);

// Write / read-back / verify / delete round-trip on a throwaway test
// file, mirroring SST25VF080B_SelfTest()'s structure. Prints a colored
// pass/fail per step and returns true only if every step passes. Backing
// call for the "SD Self Test" USB UART command.
bool SDFileIO_SelfTest(void);

#ifdef __cplusplus
}
#endif

#endif /* SD_FILEIO_H */
