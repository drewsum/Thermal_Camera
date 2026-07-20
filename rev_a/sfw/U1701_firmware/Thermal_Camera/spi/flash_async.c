/*******************************************************************************
  SPI Flash Asynchronous Read Service

  File Name:
    flash_async.c

  Summary:
    Main-loop-driven consumer of SST25VF080B_ReadAsync(). See
    flash_async.h for the layering rationale and the buffer/exclusion
    rules callers must honor.
*******************************************************************************/

#include "spi/flash_async.h"
#include "spi/device_driver/sst25vf080b.h"

static flash_async_callback_t flash_async_callback = NULL;
static bool     flash_async_active = false;
static uint32_t flash_async_overlap_count = 0;
static uint32_t flash_async_last_overlap = 0;

bool FlashAsync_ReadStart(uint32_t address, uint8_t *buffer, size_t length,
        flash_async_callback_t onComplete)
{
    if (flash_async_active)
    {
        return false;
    }

    // SST25VF080B_ReadAsync() is what enforces the alignment/size rules
    // and the "no other transfer in flight" check, and it releases chip
    // select itself if it declines -- so a false here leaves the part
    // untouched and nothing needs undoing.
    if (!SST25VF080B_ReadAsync(address, buffer, length))
    {
        return false;
    }

    flash_async_callback = onComplete;
    flash_async_overlap_count = 0;
    flash_async_active = true;

    return true;
}

void FlashAsync_Tasks(void)
{
    if (!flash_async_active)
    {
        return;
    }

    // SST25VF080B_ReadIsBusy() is also what finalizes the transfer
    // (releases CS, latches the result, makes the buffer cache-coherent),
    // so this call is the completion edge, not just a query.
    if (SST25VF080B_ReadIsBusy())
    {
        flash_async_overlap_count++;
        return;
    }

    bool success = SST25VF080B_ReadGetResult();

    // Clear state BEFORE invoking the callback so the callback is free to
    // start another read (or make blocking flash calls) without tripping
    // the still-active guard.
    flash_async_callback_t callback = flash_async_callback;
    flash_async_callback = NULL;
    flash_async_active = false;
    flash_async_last_overlap = flash_async_overlap_count;

    if (callback != NULL)
    {
        callback(success);
    }
}

bool FlashAsync_IsBusy(void)
{
    return flash_async_active;
}

uint32_t FlashAsync_GetLastOverlapCount(void)
{
    return flash_async_last_overlap;
}
