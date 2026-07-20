/*******************************************************************************
  SST25VF080B Virtual Block Disk Layer

  File Name:
    sst25vf080b_disk.c

  Summary:
    512B-sector block device over the 4KB-erase SST25VF080B, via a single
    4KB read-modify-write staging buffer. See sst25vf080b_disk.h for the
    design rationale (staging coherency, reserved last sector, durability
    contract).
*******************************************************************************/

#include <xc.h>
#include <stdio.h>
#include <string.h>

#include "spi/device_driver/sst25vf080b_disk.h"
#include "spi/device_driver/sst25vf080b.h"
#include "usb_uart/terminal_control.h"

#define FLASH_DISK_PAGE_SIZE        SST25VF080B_SECTOR_SIZE  // 4KB erase page
#define FLASH_DISK_NO_STAGED_PAGE   UINT32_MAX

static bool flash_disk_initialized = false;

// The one staging buffer -- holds the current contents of erase page
// staged_page (possibly with unsynced modifications, per staged_dirty).
// 16-byte aligned so flashDiskStagePage()'s SST25VF080B_Read() into this
// buffer can safely use spi3.c's DMA path -- see SPI3_TransferBlock()'s
// comment for why an unaligned DMA destination is a real memory-
// corruption risk (found via SST25VF080B_SelfTest() corrupting an
// adjacent buffer, 2026-07-20), not just a missed performance opportunity.
static __attribute__((aligned(16))) uint8_t staging[FLASH_DISK_PAGE_SIZE];
static uint32_t staged_page = FLASH_DISK_NO_STAGED_PAGE;
static bool staged_dirty = false;
static uint32_t last_write_tick = 0;

// Loads erase page `page` into the staging buffer, flushing whatever dirty
// page was staged before it. No-op if `page` is already staged.
static bool flashDiskStagePage(uint32_t page)
{
    if (page == staged_page)
    {
        return true;
    }

    if (!Flash_Disk_Sync())
    {
        return false;
    }

    SST25VF080B_Read(page * FLASH_DISK_PAGE_SIZE, staging, FLASH_DISK_PAGE_SIZE);
    staged_page = page;
    staged_dirty = false;
    return true;
}

bool Flash_Disk_Initialize(void)
{
    // Catch a dead/absent part here rather than as later I/O errors --
    // SST25VF080B_Initialize() itself must already have run (main.c boot
    // order), this only re-verifies the ID
    if (!SST25VF080B_Verify())
    {
        flash_disk_initialized = false;
        return false;
    }

    staged_page = FLASH_DISK_NO_STAGED_PAGE;
    staged_dirty = false;
    flash_disk_initialized = true;
    return true;
}

bool Flash_Disk_IsInitialized(void)
{
    return flash_disk_initialized;
}

uint32_t Flash_Disk_GetSectorCount(void)
{
    return FLASH_DISK_SECTOR_COUNT;
}

bool Flash_Disk_ReadSectors(uint32_t startSector, uint8_t *buffer, uint16_t sectorCount)
{
    if (!flash_disk_initialized || (buffer == NULL)
            || ((startSector + sectorCount) > FLASH_DISK_SECTOR_COUNT))
    {
        return false;
    }

    for (uint16_t i = 0; i < sectorCount; i++)
    {
        uint32_t sector = startSector + i;
        uint32_t page = sector / FLASH_DISK_SECTORS_PER_PAGE;
        uint32_t pageOffset = (sector % FLASH_DISK_SECTORS_PER_PAGE) * FLASH_DISK_SECTOR_SIZE;
        uint8_t *dest = buffer + ((uint32_t)i * FLASH_DISK_SECTOR_SIZE);

        if (page == staged_page)
        {
            // Staged page may hold newer-than-flash data -- always serve
            // from RAM so a consumer reads back its own unsynced writes
            memcpy(dest, &staging[pageOffset], FLASH_DISK_SECTOR_SIZE);
        }
        else
        {
            SST25VF080B_Read((page * FLASH_DISK_PAGE_SIZE) + pageOffset, dest,
                    FLASH_DISK_SECTOR_SIZE);
        }
    }

    return true;
}

bool Flash_Disk_WriteSectors(uint32_t startSector, const uint8_t *buffer, uint16_t sectorCount)
{
    // Refuse before touching the staging buffer: accepting sectors into
    // RAM that Flash_Disk_Sync() can never flush would be silent data loss
    if (SST25VF080B_WriteProtectIsEnabled())
    {
        return false;
    }

    if (!flash_disk_initialized || (buffer == NULL)
            || ((startSector + sectorCount) > FLASH_DISK_SECTOR_COUNT))
    {
        return false;
    }

    for (uint16_t i = 0; i < sectorCount; i++)
    {
        uint32_t sector = startSector + i;
        uint32_t page = sector / FLASH_DISK_SECTORS_PER_PAGE;
        uint32_t pageOffset = (sector % FLASH_DISK_SECTORS_PER_PAGE) * FLASH_DISK_SECTOR_SIZE;

        if (!flashDiskStagePage(page))
        {
            return false;
        }

        memcpy(&staging[pageOffset], buffer + ((uint32_t)i * FLASH_DISK_SECTOR_SIZE),
                FLASH_DISK_SECTOR_SIZE);
        staged_dirty = true;
    }

    last_write_tick = _CP0_GET_COUNT();

    return true;
}

bool Flash_Disk_Sync(void)
{
    if (!staged_dirty)
    {
        return true;
    }

    uint32_t pageAddress = staged_page * FLASH_DISK_PAGE_SIZE;

    if (!SST25VF080B_EraseSector(pageAddress))
    {
        return false;
    }

    if (!SST25VF080B_Write(pageAddress, staging, FLASH_DISK_PAGE_SIZE))
    {
        return false;
    }

    staged_dirty = false;
    return true;
}

bool Flash_Disk_IsDirty(void)
{
    return staged_dirty;
}

uint32_t Flash_Disk_TicksSinceLastWrite(void)
{
    return (uint32_t)(_CP0_GET_COUNT() - last_write_tick);
}

void Flash_Disk_PrintStatus(void)
{
    terminalTextAttributesReset();

    if (flash_disk_initialized) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Flash Disk Initialized:                   %s\n\r",
            flash_disk_initialized ? "T" : "F");

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Sector Size:                              %u bytes\n\r",
            (unsigned)FLASH_DISK_SECTOR_SIZE);
    printf("    Sector Count:                             %u (%u KB)\n\r",
            (unsigned)FLASH_DISK_SECTOR_COUNT,
            (unsigned)(FLASH_DISK_SECTOR_COUNT / 2u));
    printf("    Erase Page Size:                          %u bytes\n\r",
            (unsigned)FLASH_DISK_PAGE_SIZE);
    printf("    Reserved Self-Test Page:                  0x%05X-0x%05X\n\r",
            (unsigned)(SST25VF080B_SIZE_BYTES - SST25VF080B_SECTOR_SIZE),
            (unsigned)(SST25VF080B_SIZE_BYTES - 1u));

    if (staged_page == FLASH_DISK_NO_STAGED_PAGE)
    {
        printf("    Staged Erase Page:                        none\n\r");
    }
    else
    {
        printf("    Staged Erase Page:                        %lu (0x%05lX)\n\r",
                (unsigned long)staged_page,
                (unsigned long)(staged_page * FLASH_DISK_PAGE_SIZE));
    }

    if (staged_dirty) terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Staging Buffer Dirty:                     %s\n\r", staged_dirty ? "T" : "F");

    terminalTextAttributesReset();
}
