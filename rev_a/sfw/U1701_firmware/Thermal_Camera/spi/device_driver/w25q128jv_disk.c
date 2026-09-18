/*******************************************************************************
  W25Q128JV Virtual Block Disk Layer

  File Name:
    w25q128jv_disk.c

  Summary:
    512B-sector block device over the 4KB-erase W25Q128JV, via a single
    4KB read-modify-write staging buffer. See w25q128jv_disk.h for the
    design rationale (staging coherency, reserved last sector, durability
    contract, and why this uses the "W25Q128JV_Disk_*" prefix instead of
    the old driver's generic "Flash_Disk_*").
*******************************************************************************/

#include <xc.h>
#include <stdio.h>
#include <string.h>

#include "spi/device_driver/w25q128jv_disk.h"
#include "spi/device_driver/w25q128jv.h"
#include "usb_uart/terminal_control.h"

#define W25Q128JV_DISK_PAGE_SIZE        W25Q128JV_SECTOR_SIZE  // 4KB erase page
#define W25Q128JV_DISK_NO_STAGED_PAGE   UINT32_MAX

static bool flash_disk_initialized = false;

// The one staging buffer -- holds the current contents of erase page
// staged_page (possibly with unsynced modifications, per staged_dirty)
static uint8_t staging[W25Q128JV_DISK_PAGE_SIZE];
static uint32_t staged_page = W25Q128JV_DISK_NO_STAGED_PAGE;
static bool staged_dirty = false;
static uint32_t last_write_tick = 0;

// Loads erase page `page` into the staging buffer, flushing whatever dirty
// page was staged before it. No-op if `page` is already staged.
static bool w25q128jvDiskStagePage(uint32_t page)
{
    if (page == staged_page)
    {
        return true;
    }

    if (!W25Q128JV_Disk_Sync())
    {
        return false;
    }

    W25Q128JV_Read(page * W25Q128JV_DISK_PAGE_SIZE, staging, W25Q128JV_DISK_PAGE_SIZE);
    staged_page = page;
    staged_dirty = false;
    return true;
}

bool W25Q128JV_Disk_Initialize(void)
{
    // Catch a dead/absent part here rather than as later I/O errors --
    // W25Q128JV_Initialize() itself must already have run (main.c boot
    // order), this only re-verifies the ID
    if (!W25Q128JV_Verify())
    {
        flash_disk_initialized = false;
        return false;
    }

    staged_page = W25Q128JV_DISK_NO_STAGED_PAGE;
    staged_dirty = false;
    flash_disk_initialized = true;
    return true;
}

bool W25Q128JV_Disk_IsInitialized(void)
{
    return flash_disk_initialized;
}

uint32_t W25Q128JV_Disk_GetSectorCount(void)
{
    return W25Q128JV_DISK_SECTOR_COUNT;
}

bool W25Q128JV_Disk_ReadSectors(uint32_t startSector, uint8_t *buffer, uint16_t sectorCount)
{
    if (!flash_disk_initialized || (buffer == NULL)
            || ((startSector + sectorCount) > W25Q128JV_DISK_SECTOR_COUNT))
    {
        return false;
    }

    for (uint16_t i = 0; i < sectorCount; i++)
    {
        uint32_t sector = startSector + i;
        uint32_t page = sector / W25Q128JV_DISK_SECTORS_PER_PAGE;
        uint32_t pageOffset = (sector % W25Q128JV_DISK_SECTORS_PER_PAGE) * W25Q128JV_DISK_SECTOR_SIZE;
        uint8_t *dest = buffer + ((uint32_t)i * W25Q128JV_DISK_SECTOR_SIZE);

        if (page == staged_page)
        {
            // Staged page may hold newer-than-flash data -- always serve
            // from RAM so a consumer reads back its own unsynced writes
            memcpy(dest, &staging[pageOffset], W25Q128JV_DISK_SECTOR_SIZE);
        }
        else
        {
            W25Q128JV_Read((page * W25Q128JV_DISK_PAGE_SIZE) + pageOffset, dest,
                    W25Q128JV_DISK_SECTOR_SIZE);
        }
    }

    return true;
}

bool W25Q128JV_Disk_WriteSectors(uint32_t startSector, const uint8_t *buffer, uint16_t sectorCount)
{
    // Refuse before touching the staging buffer: accepting sectors into
    // RAM that W25Q128JV_Disk_Sync() can never flush would be silent data
    // loss
    if (W25Q128JV_WriteProtectIsEnabled())
    {
        return false;
    }

    if (!flash_disk_initialized || (buffer == NULL)
            || ((startSector + sectorCount) > W25Q128JV_DISK_SECTOR_COUNT))
    {
        return false;
    }

    for (uint16_t i = 0; i < sectorCount; i++)
    {
        uint32_t sector = startSector + i;
        uint32_t page = sector / W25Q128JV_DISK_SECTORS_PER_PAGE;
        uint32_t pageOffset = (sector % W25Q128JV_DISK_SECTORS_PER_PAGE) * W25Q128JV_DISK_SECTOR_SIZE;

        if (!w25q128jvDiskStagePage(page))
        {
            return false;
        }

        memcpy(&staging[pageOffset], buffer + ((uint32_t)i * W25Q128JV_DISK_SECTOR_SIZE),
                W25Q128JV_DISK_SECTOR_SIZE);
        staged_dirty = true;
    }

    last_write_tick = _CP0_GET_COUNT();

    return true;
}

bool W25Q128JV_Disk_Sync(void)
{
    if (!staged_dirty)
    {
        return true;
    }

    uint32_t pageAddress = staged_page * W25Q128JV_DISK_PAGE_SIZE;

    if (!W25Q128JV_EraseSector(pageAddress))
    {
        return false;
    }

    if (!W25Q128JV_Write(pageAddress, staging, W25Q128JV_DISK_PAGE_SIZE))
    {
        return false;
    }

    staged_dirty = false;
    return true;
}

bool W25Q128JV_Disk_IsDirty(void)
{
    return staged_dirty;
}

uint32_t W25Q128JV_Disk_TicksSinceLastWrite(void)
{
    return (uint32_t)(_CP0_GET_COUNT() - last_write_tick);
}

void W25Q128JV_Disk_PrintStatus(void)
{
    terminalTextAttributesReset();

    if (flash_disk_initialized) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Flash Disk Initialized:                   %s\n\r",
            flash_disk_initialized ? "T" : "F");

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Sector Size:                              %u bytes\n\r",
            (unsigned)W25Q128JV_DISK_SECTOR_SIZE);
    printf("    Sector Count:                             %u (%u KB)\n\r",
            (unsigned)W25Q128JV_DISK_SECTOR_COUNT,
            (unsigned)(W25Q128JV_DISK_SECTOR_COUNT / 2u));
    printf("    Erase Page Size:                          %u bytes\n\r",
            (unsigned)W25Q128JV_DISK_PAGE_SIZE);
    printf("    Reserved Self-Test Page:                  0x%06X-0x%06X\n\r",
            (unsigned)(W25Q128JV_SIZE_BYTES - W25Q128JV_SECTOR_SIZE),
            (unsigned)(W25Q128JV_SIZE_BYTES - 1u));

    if (staged_page == W25Q128JV_DISK_NO_STAGED_PAGE)
    {
        printf("    Staged Erase Page:                        none\n\r");
    }
    else
    {
        printf("    Staged Erase Page:                        %lu (0x%06lX)\n\r",
                (unsigned long)staged_page,
                (unsigned long)(staged_page * W25Q128JV_DISK_PAGE_SIZE));
    }

    if (staged_dirty) terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Staging Buffer Dirty:                     %s\n\r", staged_dirty ? "T" : "F");

    terminalTextAttributesReset();
}
