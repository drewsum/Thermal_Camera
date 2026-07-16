/*******************************************************************************
  FatFs Low-Level Disk I/O Glue

  File Name:
    diskio.c

  Summary:
    Bridges FatFs's disk_* API to sd_card.h. This is the only file in the
    sdhc/ tree that includes both ff.h and sd_card.h -- see sdhc.h's file
    header for why that boundary matters (it's the seam a future USB mass
    storage class driver plugs into instead of/alongside this file).

  Description:
    Not vendored from upstream FatFs -- diskio.h (the interface contract:
    DSTATUS/DRESULT/disk_* prototypes/ioctl command codes) is copied
    as-is from the FatFs R0.16 distribution, but this .c is this
    project's own glue, written against sd_card.h.
*******************************************************************************/

#include <string.h>

#include "sdhc/fatfs/ff.h"
#include "sdhc/fatfs/diskio.h"
#include "sdhc/device_driver/sd_card.h"

// Only one physical drive (pdrv 0) is supported -- FF_VOLUMES is 1
// (ffconf.h)
#define SD_DISKIO_PDRV  0u

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != SD_DISKIO_PDRV)
    {
        return STA_NOINIT;
    }

    if (!SD_Card_IsPresent())
    {
        return STA_NODISK;
    }

    return (SD_Card_GetInfo() != NULL) ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != SD_DISKIO_PDRV)
    {
        return STA_NOINIT;
    }

    return SD_Card_Initialize() ? 0 : (STA_NOINIT | STA_NODISK);
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != SD_DISKIO_PDRV)
    {
        return RES_PARERR;
    }

    return SD_Card_ReadBlocks((uint32_t)sector, buff, (uint16_t)count) ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != SD_DISKIO_PDRV)
    {
        return RES_PARERR;
    }

    return SD_Card_WriteBlocks((uint32_t)sector, buff, (uint16_t)count) ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != SD_DISKIO_PDRV)
    {
        return RES_PARERR;
    }

    const sd_card_info_t *info = SD_Card_GetInfo();

    switch (cmd)
    {
        case CTRL_SYNC:
            // sd_card.c's SD_Card_WriteBlocks() blocks until the transfer
            // completes (no write-behind cache in this driver), so
            // there's nothing pending to flush.
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
            // Erase-block size isn't tracked by this driver (would come
            // from SD Status register ERASE_SIZE, not implemented) -- 1
            // is FatFs's documented "unknown" convention for f_mkfs().
            *(DWORD *)buff = 1u;
            return RES_OK;

        default:
            return RES_PARERR;
    }
}

#if FF_FS_NORTC == 0
DWORD get_fattime(void)
{
    // Only compiled in if ffconf.h's FF_FS_NORTC is flipped to 0 -- see
    // the comment there. core/rtcc.c already has a working RTCC driver;
    // wiring it up here (packing into FatFs's documented bitfield: bit31:25
    // year-1980, bit24:21 month, bit20:16 day, bit15:11 hour, bit10:5
    // minute, bit4:0 second/2) is the follow-up this stub is left for.
    return ((DWORD)(2026 - 1980) << 25) | ((DWORD)1 << 21) | ((DWORD)1 << 16);
}
#endif
