/*******************************************************************************
  SPI Flash Asynchronous Read Service

  File Name:
    flash_async.h

  Summary:
    Main-loop-driven consumer of the SPI flash's non-blocking read path
    (SST25VF080B_ReadAsync(), spi/device_driver/sst25vf080b.h), so bulk
    flash reads overlap with the rest of the main loop instead of stalling
    it.

  Description:
    Follows the same cooperative-task shape as the other main-loop
    services (SDFileIO_HotSwapTasks(), USB_MSD_TimedTasks()): start an
    operation, then let FlashAsync_Tasks() -- pumped once per main-loop
    iteration from main.c -- carry it to completion and invoke the
    caller's callback.

    WHY THIS EXISTS SEPARATELY FROM THE FATFS PATH: FatFs's
    disk_read()/disk_write() contract requires returning with the data
    already in hand -- there is no callback or resume form -- so the
    filesystem stack (sst25vf080b_disk.c -> sdhc/fatfs/diskio.c -> FatFs
    -> flash_fileio.c) is structurally synchronous and cannot use this.
    This service is for application code that reads raw flash addresses
    directly and can tolerate a poll/callback structure. Anything going
    through a FAT file keeps using the blocking API.

    BUFFER RULES (enforced, not advisory -- see spi3.h): the destination
    must be declared __attribute__((aligned(SPI3_DMA_BUFFER_ALIGNMENT)))
    with a length that is a multiple of that alignment, must stay
    untouched by the caller until the completion callback fires, and must
    outlive the transfer (no stack buffers). A buffer that doesn't qualify
    makes FlashAsync_ReadStart() return false rather than silently doing
    something slower or unsafe.

    MUTUAL EXCLUSION: while a transfer is in flight the flash holds chip
    select asserted and the DMA channels own SPI3. Any blocking flash call
    made in that window (including the staging-buffer flush that
    USB_MSD_TimedTasks() can trigger on any main-loop pass) transparently
    waits for this transfer to finish first -- see sst25WaitForAsyncIdle()
    in sst25vf080b.c. Correctness is therefore preserved automatically,
    but a caller that starts a long read and then immediately does
    blocking flash work gets no overlap benefit.
*******************************************************************************/

#ifndef FLASH_ASYNC_H
#define FLASH_ASYNC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Invoked from FlashAsync_Tasks() (i.e. main-loop context, NOT an ISR --
// printf and further flash calls are both safe here) when a transfer
// finishes. `success` is false if the underlying DMA transfer errored or
// timed out, in which case the destination buffer's contents are
// undefined.
typedef void (*flash_async_callback_t)(bool success);

// Begins a non-blocking read of `length` bytes from raw flash `address`
// into `buffer`, and returns immediately. `onComplete` may be NULL if the
// caller would rather poll FlashAsync_IsBusy().
//
// Returns false -- nothing started, no callback will fire -- if another
// async read is already in flight, or if address/buffer/length don't
// qualify for the DMA path (see BUFFER RULES above). Callers that must
// succeed either way should fall back to the blocking SST25VF080B_Read().
bool FlashAsync_ReadStart(uint32_t address, uint8_t *buffer, size_t length,
        flash_async_callback_t onComplete);

// Pump once per main-loop iteration. Cheap no-op when idle (a single
// flag test), so it is safe to call unconditionally. When the in-flight
// transfer completes this releases the flash, latches the result, and
// invokes the callback.
void FlashAsync_Tasks(void);

// True while a transfer started by FlashAsync_ReadStart() has not yet
// completed. Note this stays true until FlashAsync_Tasks() has actually
// observed completion, so it is the flag to poll from main-loop context.
bool FlashAsync_IsBusy(void);

// Number of FlashAsync_Tasks() calls that observed the most recent
// transfer still in flight -- i.e. how many main-loop iterations ran
// concurrently with it. This is the concrete measure of how much overlap
// the async path actually bought; a value of 0 means the transfer
// finished before the main loop came back around and the caller would
// have been just as well served by the blocking API.
uint32_t FlashAsync_GetLastOverlapCount(void);

#ifdef __cplusplus
}
#endif

#endif /* FLASH_ASYNC_H */
