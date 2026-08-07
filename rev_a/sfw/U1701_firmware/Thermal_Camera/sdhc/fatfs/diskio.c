/*******************************************************************************
  FatFs Low-Level Disk I/O Glue

  File Name:
    diskio.c

  Summary:
    Bridges FatFs's disk_* API to sd_card.h. This is the only file in the
    sdhc/ tree that includes both ff.h and sd_card.h -- see sdhc.h's file
    header for why that boundary matters (it's the seam a future USB mass
    storage class driver plugs into instead of/alongside this file).

    Also home to get_fattime(), the other half of FatFs's platform contract:
    the clock every file and directory timestamp on both volumes comes from.

  Description:
    Not vendored from upstream FatFs -- diskio.h (the interface contract:
    DSTATUS/DRESULT/disk_* prototypes/ioctl command codes) is copied
    as-is from the FatFs R0.16 distribution, but this .c is this
    project's own glue, written against sd_card.h.
*******************************************************************************/

#include <string.h>

#include "core/rtcc.h"
#include "sdhc/fatfs/ff.h"
#include "sdhc/fatfs/diskio.h"
#include "sdhc/device_driver/sd_card.h"
#include "spi/device_driver/w25q128jv_disk.h"
#include "spi/device_driver/w25q128jv.h"

// Physical drive mapping (FF_VOLUMES is 2 in ffconf.h):
//   pdrv 0 = SD card (sd_card.h), FatFs's default drive
//   pdrv 1 = SPI flash (w25q128jv_disk.h), always present
#define SD_DISKIO_PDRV      0u
#define FLASH_DISKIO_PDRV   1u

DSTATUS disk_status(BYTE pdrv)
{
    switch (pdrv)
    {
        case SD_DISKIO_PDRV:
            if (!SD_Card_IsPresent())
            {
                return STA_NODISK;
            }
            return (SD_Card_GetInfo() != NULL) ? 0 : STA_NOINIT;

        case FLASH_DISKIO_PDRV:
            // Soldered-down media -- never STA_NODISK, only
            // initialized-or-not. STA_PROTECT makes FatFs return
            // FR_WRITE_PROTECTED from every write API (f_write, f_mkfs,
            // f_setlabel...) while the hardware write protect is on.
            if (!W25Q128JV_Disk_IsInitialized())
            {
                return STA_NOINIT;
            }
            return W25Q128JV_WriteProtectIsEnabled() ? STA_PROTECT : 0;

        default:
            return STA_NOINIT;
    }
}

DSTATUS disk_initialize(BYTE pdrv)
{
    switch (pdrv)
    {
        case SD_DISKIO_PDRV:
            return SD_Card_Initialize() ? 0 : (STA_NOINIT | STA_NODISK);

        case FLASH_DISKIO_PDRV:
            if (!W25Q128JV_Disk_Initialize())
            {
                return STA_NOINIT;
            }
            // f_mkfs checks THIS return for STA_PROTECT (mount-time
            // writes check disk_status above)
            return W25Q128JV_WriteProtectIsEnabled() ? STA_PROTECT : 0;

        default:
            return STA_NOINIT;
    }
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    switch (pdrv)
    {
        case SD_DISKIO_PDRV:
            return SD_Card_ReadBlocks((uint32_t)sector, buff, (uint16_t)count) ? RES_OK : RES_ERROR;

        case FLASH_DISKIO_PDRV:
            return W25Q128JV_Disk_ReadSectors((uint32_t)sector, buff, (uint16_t)count) ? RES_OK : RES_ERROR;

        default:
            return RES_PARERR;
    }
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    switch (pdrv)
    {
        case SD_DISKIO_PDRV:
            return SD_Card_WriteBlocks((uint32_t)sector, buff, (uint16_t)count) ? RES_OK : RES_ERROR;

        case FLASH_DISKIO_PDRV:
            return W25Q128JV_Disk_WriteSectors((uint32_t)sector, buff, (uint16_t)count) ? RES_OK : RES_ERROR;

        default:
            return RES_PARERR;
    }
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv == SD_DISKIO_PDRV)
    {
        const sd_card_info_t *info = SD_Card_GetInfo();

        switch (cmd)
        {
            case CTRL_SYNC:
                // sd_card.c's SD_Card_WriteBlocks() blocks until the
                // transfer completes (no write-behind cache in this
                // driver), so there's nothing pending to flush.
                return RES_OK;

            case GET_SECTOR_COUNT:
                if (info == NULL)
                {
                    return RES_NOTRDY;
                }
                *(LBA_t *)buff = (LBA_t)info->capacity_blocks;
                return RES_OK;

            case GET_SECTOR_SIZE:
                *(WORD *)buff = 512u;
                return RES_OK;

            case GET_BLOCK_SIZE:
                // Erase-block size isn't tracked by this driver (would
                // come from SD Status register ERASE_SIZE, not
                // implemented) -- 1 is FatFs's documented "unknown"
                // convention for f_mkfs().
                *(DWORD *)buff = 1u;
                return RES_OK;

            default:
                return RES_PARERR;
        }
    }

    if (pdrv == FLASH_DISKIO_PDRV)
    {
        switch (cmd)
        {
            case CTRL_SYNC:
                // Unlike the SD path, this one is real: the flash disk
                // layer write-caches a 4KB erase page (see
                // w25q128jv_disk.h) that must be flushed for f_sync()/
                // f_close() to actually be durable.
                return W25Q128JV_Disk_Sync() ? RES_OK : RES_ERROR;

            case GET_SECTOR_COUNT:
                *(LBA_t *)buff = (LBA_t)W25Q128JV_Disk_GetSectorCount();
                return RES_OK;

            case GET_SECTOR_SIZE:
                *(WORD *)buff = W25Q128JV_DISK_SECTOR_SIZE;
                return RES_OK;

            case GET_BLOCK_SIZE:
                // True erase-block ratio (4KB page / 512B sector) so
                // f_mkfs() aligns the data area to erase boundaries
                *(DWORD *)buff = W25Q128JV_DISK_SECTORS_PER_PAGE;
                return RES_OK;

            default:
                return RES_PARERR;
        }
    }

    return RES_PARERR;
}

#if FF_FS_NORTC == 0
DWORD get_fattime(void)
{
    uint16_t year;
    uint8_t month, day, hours, minutes, seconds;
    uint32_t attempt;

    // rtcc_shadow is refreshed field by field from the RTCC alarm ISR (once a
    // second, IPL3), so a copy taken from main context can straddle an update
    // and mix pre- and post-update fields. Most of the time that would cost a
    // second; across midnight it would cost a whole day. Seconds is the last
    // field the ISR writes, so bracketing the copy with it detects the case
    // that matters -- a full ISR run landing inside the copy -- and a retry
    // gets a consistent set. Bounded so a stalled RTCC cannot hang a file
    // write; the worst a torn read survives is a sub-second skew, which FAT's
    // two-second timestamp resolution mostly swallows anyway.
    for (attempt = 0; attempt < 3u; attempt++)
    {
        seconds = rtcc_shadow.seconds;
        minutes = rtcc_shadow.minutes;
        hours   = rtcc_shadow.hours;
        day     = rtcc_shadow.day;
        month   = rtcc_shadow.month;
        year    = rtcc_shadow.year;

        if (seconds == rtcc_shadow.seconds) break;
    }

    // Clamp to what the FAT fields can hold. rtccClear() leaves the RTCC at
    // 2000-01-01 00:00:00 on a cold boot, which is in range, but a shadow
    // that was never filled (RTCC init failed, or a write somehow beats
    // rtccInitialize()) reads as all zeros and would encode a negative year
    // into bits 31:25. A wrong-but-legal date beats a corrupt directory
    // entry -- and 1980-01-01 is recognisable as "clock was never set".
    if (year < 1980u) year = 1980u;
    else if (year > 2107u) year = 2107u;
    if ((month < 1u) || (month > 12u)) month = 1u;
    if ((day < 1u) || (day > 31u)) day = 1u;
    if (hours > 23u) hours = 0u;
    if (minutes > 59u) minutes = 0u;
    if (seconds > 59u) seconds = 0u;

    // FatFs's documented packing: bit31:25 year-1980, bit24:21 month,
    // bit20:16 day, bit15:11 hour, bit10:5 minute, bit4:0 second/2
    return ((DWORD)(year - 1980u) << 25)
            | ((DWORD)month << 21)
            | ((DWORD)day << 16)
            | ((DWORD)hours << 11)
            | ((DWORD)minutes << 5)
            | ((DWORD)(seconds / 2u));
}
#endif
