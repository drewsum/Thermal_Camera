/*******************************************************************************
  SD Card File I/O Helpers

  File Name:
    sd_fileio.c

  Summary:
    Thin app-facing FatFs wrappers. See sd_fileio.h for the role this
    plays relative to sd_card.c/sdhc.c.
*******************************************************************************/

#include <stdio.h>
#include <string.h>

#include "sdhc/sd_fileio.h"
#include "sdhc/fatfs/ff.h"
#include "sdhc/device_driver/sd_card.h"
#include "usb_uart/terminal_control.h"
#include "usb/device_driver/usb_msd.h"

// While a USB host owns the media (usb_msd.h yield-to-host policy), all
// local file I/O must refuse -- host and firmware writing the same FAT
// volume corrupts it. Prints why, so a console user isn't left guessing.
static bool sdFileIOMediaAvailable(void)
{
    if (usb_msd_media_owned_by_host)
    {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    SD card is owned by the USB host -- unplug USB or send 'USB Detach'\r\n");
        terminalTextAttributesReset();
        return false;
    }
    return true;
}

// 8.3-compliant name, unlikely to collide with a real file -- SD_TMP is a
// dedicated throwaway name for SDFileIO_SelfTest(), not a scratch area
// for general use.
#define SD_SELFTEST_FILENAME  "/SD_TEST.TMP"
#define SD_SELFTEST_PATTERN   "Thermal_Camera SD self-test 0123456789 ABCDEFGHIJ"

static FATFS sd_fatfs;
static bool sd_mounted = false;

bool SDFileIO_Mount(void)
{
    if (!sdFileIOMediaAvailable())
    {
        return false;
    }

    // opt=1: mount now rather than lazily on first file access, so a
    // bad/unformatted/absent card is reported immediately
    FRESULT fr = f_mount(&sd_fatfs, "", 1);
    sd_mounted = (fr == FR_OK);
    return sd_mounted;
}

bool SDFileIO_Unmount(void)
{
    if (!sdFileIOMediaAvailable())
    {
        return false;
    }

    f_mount(NULL, "", 0);
    sd_mounted = false;

    // State teardown only -- SD_PWR_EN_PIN stays HIGH. Card-detect
    // sensing runs off the switched rail, so powering down here (as this
    // originally did) makes an empty slot read "card present" and fed an
    // infinite remount loop on removal. See SD_Card_PowerDown(), which is
    // reserved for sleep entry.
    SD_Card_Deinitialize();
    return true;
}

bool SDFileIO_UnmountKeepPower(void)
{
    // No SD_Card_PowerDown() here -- see the header comment: the card is
    // being handed to the USB host, not ejected
    f_mount(NULL, "", 0);
    sd_mounted = false;
    return true;
}

void SDFileIO_HotSwapTasks(void)
{
    if (!sd_card_hotswap_event)
    {
        return;
    }
    sd_card_hotswap_event = 0;

    if (usb_msd_media_owned_by_host)
    {
        // USB MSC layer owns presence tracking while the host holds the
        // media (see header comment) -- swallow the event; the release
        // path re-checks presence and remounts on its own
        return;
    }

    // SD_Card_IsPresent() is the debounce (5 consecutive consistent 1ms
    // reads), so slot-contact bounce collapses into one settled answer.
    // Bounce edges that re-latch the event after this read just cause
    // extra passes through here that no-op on the state comparison below.
    bool present = SD_Card_IsPresent();
    bool initialized = (SD_Card_GetInfo() != NULL);

    if (present && !initialized)
    {
        // The CN edge fires at first contact touch, well before the card
        // is fully seated -- contacts wipe and the card can brown in and
        // out of power for tens of ms while the user is still pushing it
        // in. Give it time to seat and finish its own power-on reset,
        // then re-verify it's still there before the first command.
        // Without this, CMD8 lands on a card that can't answer yet, and a
        // missed CMD8 costs the full 1s ACMD41 timeout (see the CMD8
        // retry comment in sd_card.c) -- mounting then only succeeded
        // when a later bounce edge happened to retry.
        SD_Card_DelayUs(100000u);
        if (!SD_Card_IsPresent())
        {
            // Still moving (or actually a removal) -- whatever changed
            // will have latched a fresh event; deal with it next pass
            return;
        }

        terminalTextAttributes(MAGENTA_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("microSD card inserted, mounting:\r\n");
        terminalTextAttributesReset();

        // One deliberate retry instead of relying on slot-bounce edges to
        // re-trigger this path
        bool mounted = SD_Card_Initialize() && SDFileIO_Mount();
        if (!mounted)
        {
            mounted = SD_Card_Initialize() && SDFileIO_Mount();
        }

        if (mounted)
        {
            SDFileIO_EnsureLabel();
            terminalTextAttributes(MAGENTA_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("    microSD card mounted\r\n");
            terminalTextAttributesReset();
            SD_Card_PrintInfo();
        }
        else
        {
            terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("    microSD card failed to mount\r\n");
        }
        terminalTextAttributesReset();
    }
    else if (!present && initialized)
    {
        terminalTextAttributes(MAGENTA_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("microSD card removed, unmounting\r\n");
        terminalTextAttributesReset();

        // The card is already gone, so this is cleanup, not a graceful
        // eject: f_mount(NULL) drops the FatFs view (any open files are
        // simply orphaned) and the card state cache is invalidated. Slot
        // power stays on -- card-detect depends on it (see
        // SD_Card_PowerDown()).
        SDFileIO_Unmount();
    }
}

bool SDFileIO_EnsureLabel(void)
{
    if (!sd_mounted)
    {
        return false;
    }

    char label[24];
    DWORD vsn;
    if (f_getlabel("", label, &vsn) != FR_OK)
    {
        return false;
    }

    // FAT labels cap at 11 chars; Windows Explorer prefers the
    // full-length label= from autorun.inf, so create that once if absent
    // (FR_EXIST is the common case)
    static const char autorun[] = "[autorun]\r\nlabel=Thermal Camera SD\r\n";
    FIL file;
    if (f_open(&file, "autorun.inf", FA_CREATE_NEW | FA_WRITE) == FR_OK)
    {
        UINT written;
        f_write(&file, autorun, sizeof(autorun) - 1u, &written);
        f_close(&file);
    }

    if ((label[0] != '\0') && (strcmp(label, "SD") != 0))
    {
        // Card already has a label (possibly the user's own) -- leave it
        return true;
    }

    // Blank, or still the old firmware default "SD"
    return (f_setlabel("THERMAL SD") == FR_OK);
}

bool SDFileIO_ListFiles(const char *path, void (*printLine)(const char *line))
{
    if (!sdFileIOMediaAvailable() || !sd_mounted || (printLine == NULL))
    {
        return false;
    }

    const char *dirPath = ((path != NULL) && (path[0] != '\0')) ? path : "/";

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

bool SDFileIO_ReadTextFileToTerminal(const char *path)
{
    if (!sdFileIOMediaAvailable() || !sd_mounted || (path == NULL))
    {
        return false;
    }

    FIL file;
    if (f_open(&file, path, FA_READ) != FR_OK)
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

bool SDFileIO_WriteFile(const char *path, const uint8_t *data, size_t length)
{
    if (!sdFileIOMediaAvailable() || !sd_mounted || (path == NULL) || (data == NULL))
    {
        return false;
    }

    FIL file;
    if (f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
    {
        return false;
    }

    UINT bytesWritten = 0;
    FRESULT fr = f_write(&file, data, (UINT)length, &bytesWritten);
    f_close(&file);

    return (fr == FR_OK) && (bytesWritten == (UINT)length);
}

bool SDFileIO_DeleteFile(const char *path)
{
    if (!sdFileIOMediaAvailable() || !sd_mounted || (path == NULL))
    {
        return false;
    }

    return (f_unlink(path) == FR_OK);
}

bool SDFileIO_GetVolumeInfo(char *fsTypeStr, size_t fsTypeStrSize,
        char *labelStr, size_t labelStrSize, uint32_t *totalKB, uint32_t *freeKB)
{
    if (!sdFileIOMediaAvailable() || !sd_mounted)
    {
        return false;
    }

    DWORD freeClusters;
    FATFS *fsPtr = &sd_fatfs;
    if (f_getfree("", &freeClusters, &fsPtr) != FR_OK)
    {
        return false;
    }

    if (totalKB != NULL)
    {
        uint32_t totalSectors = (sd_fatfs.n_fatent - 2u) * sd_fatfs.csize;
        *totalKB = totalSectors / 2u; // 512-byte sectors -> KB
    }

    if (freeKB != NULL)
    {
        uint32_t freeSectors = freeClusters * sd_fatfs.csize;
        *freeKB = freeSectors / 2u;
    }

    if ((fsTypeStr != NULL) && (fsTypeStrSize > 0u))
    {
        const char *typeName;
        switch (sd_fatfs.fs_type)
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
        if (f_getlabel("", rawLabel, &vsn) != FR_OK)
        {
            rawLabel[0] = '\0';
        }
        strncpy(labelStr, rawLabel, labelStrSize - 1u);
        labelStr[labelStrSize - 1u] = '\0';
    }

    return true;
}

bool SDFileIO_SelfTest(void)
{
    bool overallPass = true;
    char readBuffer[sizeof(SD_SELFTEST_PATTERN)];

    terminalTextAttributesReset();
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("SD Card File I/O Self-Test:\r\n");

    if (!sdFileIOMediaAvailable())
    {
        return false;
    }

    if (!sd_mounted)
    {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    FAIL: no FAT volume currently mounted\r\n");
        terminalTextAttributesReset();
        return false;
    }

    // ---- Write ----
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    [1/3] Write %s (%u bytes): ", SD_SELFTEST_FILENAME, (unsigned)sizeof(SD_SELFTEST_PATTERN) - 1u);
    if (SDFileIO_WriteFile(SD_SELFTEST_FILENAME, (const uint8_t *)SD_SELFTEST_PATTERN, sizeof(SD_SELFTEST_PATTERN) - 1u))
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

    FIL file;
    bool readOk = false;
    if (f_open(&file, SD_SELFTEST_FILENAME, FA_READ) == FR_OK)
    {
        UINT bytesRead = 0;
        readOk = (f_read(&file, readBuffer, sizeof(SD_SELFTEST_PATTERN) - 1u, &bytesRead) == FR_OK)
                && (bytesRead == (sizeof(SD_SELFTEST_PATTERN) - 1u))
                && (memcmp(readBuffer, SD_SELFTEST_PATTERN, sizeof(SD_SELFTEST_PATTERN) - 1u) == 0);
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
    if (SDFileIO_DeleteFile(SD_SELFTEST_FILENAME))
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

    terminalTextAttributes(overallPass ? GREEN_COLOR : RED_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    Overall: %s\r\n", overallPass ? "PASS" : "FAIL");
    terminalTextAttributesReset();

    return overallPass;
}
