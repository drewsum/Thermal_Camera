/*******************************************************************************
  SPI Flash File I/O Helpers

  File Name:
    flash_fileio.c

  Summary:
    Thin app-facing FatFs wrappers for the SPI flash volume ("1:"). See
    flash_fileio.h for the role this plays relative to sd_fileio.c and
    sst25vf080b_disk.c.
*******************************************************************************/

#include <stdio.h>
#include <string.h>

#include "spi/flash_fileio.h"
#include "spi/device_driver/sst25vf080b_disk.h"
#include "spi/device_driver/sst25vf080b.h"
#include "sdhc/fatfs/ff.h"
#include "usb_uart/terminal_control.h"
#include "usb/device_driver/usb_msd.h"

// While a USB host owns the media (usb_msd.h yield-to-host policy), all
// local file I/O must refuse -- host and firmware writing the same FAT
// volume corrupts it. Prints why, so a console user isn't left guessing.
static bool flashFileIOMediaAvailable(void)
{
    if (usb_msd_media_owned_by_host)
    {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    SPI flash volume is owned by the USB host -- unplug USB or send 'USB Detach'\r\n");
        terminalTextAttributesReset();
        return false;
    }
    return true;
}

#define FLASH_DRIVE_PREFIX      "1:"
#define FLASH_SELFTEST_FILENAME "1:/FL_TEST.TMP"
#define FLASH_SELFTEST_PATTERN  "Thermal_Camera SPI flash FS self-test 0123456789"

static FATFS flash_fatfs;
static bool flash_mounted = false;

// f_mkfs() scratch space -- FF_MAX_SS (512B) is the minimum legal size;
// bigger only speeds formatting up, and this volume is 1MB, so minimum it
// is. Deliberately NOT the disk layer's 4KB staging buffer: mkfs writes
// this buffer out through Flash_Disk_WriteSectors(), which copies into
// that staging buffer -- sharing them would make those copies
// self-overlapping.
static BYTE mkfs_work[FF_MAX_SS];

// FAT/SFD, everything else auto-selected by f_mkfs() from the disk
// geometry (2040 sectors -> FAT12)
static const MKFS_PARM flash_mkfs_parm = {
    .fmt = FM_FAT | FM_SFD,
    .n_fat = 0,
    .align = 0,
    .n_root = 0,
    .au_size = 0
};

// Applies FLASH_FILEIO_VOLUME_LABEL if the mounted volume's label is
// currently blank or still an old firmware default -- a deliberately
// renamed volume stays renamed
static void flashFileIOEnsureLabel(void)
{
    char label[24];
    DWORD vsn;

    // Nothing to do (and nothing wrong) while the part is write
    // protected -- which is the normal boot state, since
    // SST25VF080B_Initialize() deliberately ends PROTECTED. Bailing here
    // rather than letting f_setlabel() come back FR_WRITE_PROTECTED keeps
    // the expected case from printing a scary diagnostic on every boot.
    if (SST25VF080B_WriteProtectIsEnabled())
    {
        return;
    }

    if (f_getlabel(FLASH_DRIVE_PREFIX, label, &vsn) == FR_OK
            && ((label[0] == '\0') || (strcmp(label, "FLASH") == 0)))
    {
        FRESULT fr = f_setlabel(FLASH_DRIVE_PREFIX FLASH_FILEIO_VOLUME_LABEL);
        if (fr != FR_OK)
        {
            terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("    flashFileIOEnsureLabel: f_setlabel failed (FRESULT %d)\r\n", (int)fr);
            terminalTextAttributesReset();
        }
    }
}

// FAT volume labels cap at 11 chars, but Windows Explorer displays the
// full-length label= from autorun.inf instead when one exists -- create
// it once if absent (never overwrite: FR_EXIST is the common case)
static void flashFileIOEnsureAutorun(void)
{
    static const char autorun[] =
            "[autorun]\r\nlabel=Thermal Camera SPI Flash\r\n";
    FIL file;

    // Same rationale as flashFileIOEnsureLabel(): a write-protected part
    // is the normal boot state, and FatFs checks write protection BEFORE
    // file existence, so this would report FR_WRITE_PROTECTED every boot
    // even though autorun.inf is already present and nothing is wrong.
    if (SST25VF080B_WriteProtectIsEnabled())
    {
        return;
    }

    FRESULT fr = f_open(&file, FLASH_DRIVE_PREFIX "/autorun.inf", FA_CREATE_NEW | FA_WRITE);

    if (fr == FR_OK)
    {
        UINT written = 0;
        FRESULT writeFr = f_write(&file, autorun, sizeof(autorun) - 1u, &written);
        FRESULT closeFr = f_close(&file);

        if ((writeFr != FR_OK) || (written != (sizeof(autorun) - 1u)) || (closeFr != FR_OK))
        {
            terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("    flashFileIOEnsureAutorun: write/close failed (write FRESULT %d, wrote %u/%u, close FRESULT %d)\r\n",
                    (int)writeFr, (unsigned)written, (unsigned)(sizeof(autorun) - 1u), (int)closeFr);
            terminalTextAttributesReset();
        }
    }
    else if (fr != FR_EXIST)
    {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    flashFileIOEnsureAutorun: f_open failed (FRESULT %d)\r\n", (int)fr);
        terminalTextAttributesReset();
    }
}

bool FlashFileIO_MountAndFormatIfNeeded(void)
{
    if (!flashFileIOMediaAvailable())
    {
        return false;
    }

    // opt=1: mount now rather than lazily, so an unformatted part is
    // detected (and handled) here instead of on the first file access
    FRESULT fr = f_mount(&flash_fatfs, FLASH_DRIVE_PREFIX, 1);

    if (fr == FR_NO_FILESYSTEM)
    {
        // Fresh/erased part -- expected on first boot, not an error;
        // build the volume now. Provisioning temporarily lifts the
        // boot-default hardware write protect (restored below), otherwise
        // a virgin board could never build its own volume.
        bool reprotect = SST25VF080B_WriteProtectIsEnabled();

        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    No FAT volume on SPI flash, formatting%s...\r\n",
                reprotect ? " (write protect lifted for provisioning)" : "");
        terminalTextAttributesReset();

        if (reprotect && !SST25VF080B_WriteProtectSet(false))
        {
            flash_mounted = false;
            return false;
        }

        FRESULT mkfsFr = f_mkfs(FLASH_DRIVE_PREFIX, &flash_mkfs_parm, mkfs_work, sizeof(mkfs_work));
        bool mkfsOk = (mkfsFr == FR_OK);

        if (!mkfsOk)
        {
            terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("    f_mkfs failed (FRESULT %d)\r\n", (int)mkfsFr);
            terminalTextAttributesReset();
        }

        if (mkfsOk)
        {
            fr = f_mount(&flash_fatfs, FLASH_DRIVE_PREFIX, 1);
            if (fr != FR_OK)
            {
                terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
                printf("    post-mkfs f_mount failed (FRESULT %d)\r\n", (int)fr);
                terminalTextAttributesReset();
            }
            else
            {
                // Label/autorun also need WP off, so provision them
                // before re-protecting (the post-if copies of these
                // calls then no-op)
                flashFileIOEnsureLabel();
                flashFileIOEnsureAutorun();
                Flash_Disk_Sync();
            }
        }

        if (reprotect)
        {
            SST25VF080B_WriteProtectSet(true);
        }

        if (!mkfsOk)
        {
            flash_mounted = false;
            return false;
        }
    }

    flash_mounted = (fr == FR_OK);

    if (flash_mounted)
    {
        flashFileIOEnsureLabel();
        flashFileIOEnsureAutorun();
        // Label/autorun writes may be sitting in the staging buffer --
        // make the volume durable before declaring the mount good
        Flash_Disk_Sync();
    }

    return flash_mounted;
}

bool FlashFileIO_Unmount(void)
{
    f_mount(NULL, FLASH_DRIVE_PREFIX, 0);
    flash_mounted = false;

    // Nothing may be left RAM-only once we're unmounted -- the next
    // consumer (USB host) reads the raw flash
    return Flash_Disk_Sync();
}

bool FlashFileIO_IsMounted(void)
{
    return flash_mounted;
}

bool FlashFileIO_Format(void)
{
    if (!flashFileIOMediaAvailable())
    {
        return false;
    }

    // Unmount first so FatFs holds no stale volume state across the format
    f_mount(NULL, FLASH_DRIVE_PREFIX, 0);
    flash_mounted = false;

    FRESULT mkfsFr = f_mkfs(FLASH_DRIVE_PREFIX, &flash_mkfs_parm, mkfs_work, sizeof(mkfs_work));
    if (mkfsFr != FR_OK)
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    f_mkfs failed (FRESULT %d)\r\n", (int)mkfsFr);
        terminalTextAttributesReset();
        return false;
    }

    FRESULT mountFr = f_mount(&flash_fatfs, FLASH_DRIVE_PREFIX, 1);
    if (mountFr != FR_OK)
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    post-mkfs f_mount failed (FRESULT %d)\r\n", (int)mountFr);
        terminalTextAttributesReset();
        return false;
    }

    flash_mounted = true;

    FRESULT labelFr = f_setlabel(FLASH_DRIVE_PREFIX FLASH_FILEIO_VOLUME_LABEL);
    if (labelFr != FR_OK)
    {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    f_setlabel failed (FRESULT %d)\r\n", (int)labelFr);
        terminalTextAttributesReset();
    }

    flashFileIOEnsureAutorun();
    return Flash_Disk_Sync();
}

bool FlashFileIO_ListFiles(const char *path, void (*printLine)(const char *line))
{
    if (!flashFileIOMediaAvailable() || !flash_mounted || (printLine == NULL))
    {
        return false;
    }

    // Default to the volume root; prefix relative paths so they land on
    // this volume rather than FatFs's default drive (0: = SD card)
    char dirPath[64];
    if ((path == NULL) || (path[0] == '\0'))
    {
        strcpy(dirPath, FLASH_DRIVE_PREFIX "/");
    }
    else if ((path[0] == '0' || path[0] == '1') && (path[1] == ':'))
    {
        strncpy(dirPath, path, sizeof(dirPath) - 1u);
        dirPath[sizeof(dirPath) - 1u] = '\0';
    }
    else
    {
        snprintf(dirPath, sizeof(dirPath), FLASH_DRIVE_PREFIX "%s", path);
    }

    DIR dir;
    if (f_opendir(&dir, dirPath) != FR_OK)
    {
        return false;
    }

    char lineBuf[64];
    FILINFO fno;
    FRESULT fr;

    for (;;)
    {
        fr = f_readdir(&dir, &fno);
        if ((fr != FR_OK) || (fno.fname[0] == 0))
        {
            break;
        }

        snprintf(lineBuf, sizeof(lineBuf), "%s%-13s %10lu",
                (fno.fattrib & AM_DIR) ? "[DIR]  " : "       ",
                fno.fname, (unsigned long)fno.fsize);
        printLine(lineBuf);
    }

    f_closedir(&dir);
    return true;
}

bool FlashFileIO_ReadTextFileToTerminal(const char *path)
{
    if (!flashFileIOMediaAvailable() || !flash_mounted || (path == NULL))
    {
        return false;
    }

    // Prefix relative paths so they land on this volume rather than
    // FatFs's default drive (0: = SD card) -- same rule as ListFiles
    char filePath[64];
    if ((path[0] == '0' || path[0] == '1') && (path[1] == ':'))
    {
        strncpy(filePath, path, sizeof(filePath) - 1u);
        filePath[sizeof(filePath) - 1u] = '\0';
    }
    else
    {
        snprintf(filePath, sizeof(filePath), FLASH_DRIVE_PREFIX "%s", path);
    }

    FIL file;
    if (f_open(&file, filePath, FA_READ) != FR_OK)
    {
        return false;
    }

    char buffer[128];
    UINT bytesRead;
    FRESULT fr;

    do
    {
        fr = f_read(&file, buffer, sizeof(buffer) - 1u, &bytesRead);
        if ((fr == FR_OK) && (bytesRead > 0u))
        {
            buffer[bytesRead] = '\0';
            printf("%s", buffer);
        }
    } while ((fr == FR_OK) && (bytesRead == (sizeof(buffer) - 1u)));

    printf("\r\n");
    f_close(&file);

    return (fr == FR_OK);
}

bool FlashFileIO_GetVolumeInfo(char *fsTypeStr, size_t fsTypeStrSize,
        char *labelStr, size_t labelStrSize, uint32_t *totalKB, uint32_t *freeKB,
        uint32_t *totalBytes, uint32_t *freeBytes)
{
    if (!flashFileIOMediaAvailable() || !flash_mounted)
    {
        return false;
    }

    DWORD freeClusters;
    FATFS *fsPtr = &flash_fatfs;
    if (f_getfree(FLASH_DRIVE_PREFIX, &freeClusters, &fsPtr) != FR_OK)
    {
        return false;
    }

    uint32_t totalSectors = (flash_fatfs.n_fatent - 2u) * flash_fatfs.csize;
    uint32_t freeSectors = freeClusters * flash_fatfs.csize;

    if (totalKB != NULL) *totalKB = totalSectors / 2u; // 512-byte sectors -> KB
    if (freeKB != NULL) *freeKB = freeSectors / 2u;

    // Computed from the sector count directly (not totalKB*1024) so these
    // stay byte-exact instead of inheriting the KB conversion's truncation
    if (totalBytes != NULL) *totalBytes = totalSectors * 512u;
    if (freeBytes != NULL) *freeBytes = freeSectors * 512u;

    if ((fsTypeStr != NULL) && (fsTypeStrSize > 0u))
    {
        const char *typeName;
        switch (flash_fatfs.fs_type)
        {
            case FS_FAT12: typeName = "FAT12"; break;
            case FS_FAT16: typeName = "FAT16"; break;
            case FS_FAT32: typeName = "FAT32"; break;
            default:       typeName = "Unknown"; break;
        }
        strncpy(fsTypeStr, typeName, fsTypeStrSize - 1u);
        fsTypeStr[fsTypeStrSize - 1u] = '\0';
    }

    if ((labelStr != NULL) && (labelStrSize > 0u))
    {
        DWORD vsn;
        char rawLabel[24];
        if (f_getlabel(FLASH_DRIVE_PREFIX, rawLabel, &vsn) != FR_OK)
        {
            rawLabel[0] = '\0';
        }
        strncpy(labelStr, rawLabel, labelStrSize - 1u);
        labelStr[labelStrSize - 1u] = '\0';
    }

    return true;
}

bool FlashFileIO_SelfTest(void)
{
    bool overallPass = true;
    char readBuffer[sizeof(FLASH_SELFTEST_PATTERN)];

    terminalTextAttributesReset();
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("SPI Flash File I/O Self-Test:\r\n");

    if (!flashFileIOMediaAvailable())
    {
        return false;
    }

    if (!flash_mounted)
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    FAIL: flash FAT volume not currently mounted\r\n");
        terminalTextAttributesReset();
        return false;
    }

    // ---- Write ----
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    [1/3] Write %s (%u bytes): ", FLASH_SELFTEST_FILENAME,
            (unsigned)sizeof(FLASH_SELFTEST_PATTERN) - 1u);

    bool writeOk = false;
    FIL file;
    if (f_open(&file, FLASH_SELFTEST_FILENAME, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
    {
        UINT bytesWritten = 0;
        writeOk = (f_write(&file, FLASH_SELFTEST_PATTERN,
                        sizeof(FLASH_SELFTEST_PATTERN) - 1u, &bytesWritten) == FR_OK)
                && (bytesWritten == (sizeof(FLASH_SELFTEST_PATTERN) - 1u));
        f_close(&file);
    }

    if (writeOk)
    {
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("PASS\r\n");
    }
    else
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("FAIL\r\n");
        overallPass = false;
    }

    // ---- Readback verify ----
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    [2/3] Readback verify: ");
    memset(readBuffer, 0, sizeof(readBuffer));

    bool readOk = false;
    if (f_open(&file, FLASH_SELFTEST_FILENAME, FA_READ) == FR_OK)
    {
        UINT bytesRead = 0;
        readOk = (f_read(&file, readBuffer, sizeof(FLASH_SELFTEST_PATTERN) - 1u, &bytesRead) == FR_OK)
                && (bytesRead == (sizeof(FLASH_SELFTEST_PATTERN) - 1u))
                && (memcmp(readBuffer, FLASH_SELFTEST_PATTERN, sizeof(FLASH_SELFTEST_PATTERN) - 1u) == 0);
        f_close(&file);
    }

    if (readOk)
    {
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("PASS\r\n");
    }
    else
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("FAIL (content mismatch or read error)\r\n");
        overallPass = false;
    }

    // ---- Delete ----
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    [3/3] Delete: ");
    if (f_unlink(FLASH_SELFTEST_FILENAME) == FR_OK)
    {
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("PASS\r\n");
    }
    else
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("FAIL\r\n");
        overallPass = false;
    }

    // Leave nothing pending in the staging buffer after a self-test pass
    Flash_Disk_Sync();

    terminalTextAttributes(overallPass ? GREEN_COLOR : RED_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    Overall: %s\r\n", overallPass ? "PASS" : "FAIL");
    terminalTextAttributesReset();

    return overallPass;
}
