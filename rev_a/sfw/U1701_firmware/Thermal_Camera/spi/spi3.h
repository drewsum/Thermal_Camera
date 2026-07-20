/*******************************************************************************
  SPI3 Master Driver

  File Name:
    spi3.h

  Summary:
    Blocking driver for the SPI3 peripheral in master mode.

  Description:
    SPI3 has exactly one device on this board (the SST25VF080B SPI NOR
    flash, sst25vf080b.c/h), so unlike i2c_master.h this driver has no
    transaction queue and no notion of addressing multiple devices -- it is
    a thin, blocking byte-shift primitive. Chip select is NOT managed here:
    it's a device-specific GPIO (nFLASH_SPI_CS_PIN, pin_macros.h) toggled by
    the device driver around a transaction, the same way SDA/SCL framing is
    generic but nFLASH_SPI_CS_PIN is device-specific.
*******************************************************************************/

#ifndef SPI3_H
#define SPI3_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/attribs.h>
// _DMA2_VECTOR/_DMA3_VECTOR (used by the ISR declarations below) come from
// the processor header xc.h pulls in. Included here rather than relying on
// every .c that includes this header to have already included xc.h first
// -- sst25vf080b.c doesn't (it includes this header before xc.h), which is
// exactly what broke the build the first time these declarations were added.
#include <xc.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint32_t clkSpeed;
} SPI3_TRANSFER_SETUP;

// Enables SPI3 in master mode, SPI Mode 0 (CPOL=0, CPHA=0 -- CKP=0, CKE=1 in
// PIC32's inverted-CPHA convention), 8-bit, software (GPIO) chip select, at
// the default clock speed (see SPI3_TransferSetup()). Must be called before
// any other SPI3_* function.
bool SPI3_Initialize(void);

// Reconfigures the bus clock speed (Hz). Do not call while a transfer is in
// progress. Pass srcClkFreq = 0 to use the default peripheral clock
// assumption (PBCLK2, see spi3.c).
bool SPI3_TransferSetup(SPI3_TRANSFER_SETUP *setup, uint32_t srcClkFreq);

// Shifts one byte out on SDO3 while simultaneously shifting one byte in
// from SDI3 (standard full-duplex SPI shift-register behavior), and
// returns the received byte. Callers that only care about one direction
// pass a dummy value (e.g. 0x00) for `data` on a read, or ignore the
// return value on a write.
uint8_t SPI3_TransferByte(uint8_t data);

// Required alignment (and length granularity) for any buffer that wants
// the DMA path in SPI3_TransferBlock()/SPI3_TransferBlockAsync(). This is
// the D-cache line size: the post-transfer invalidate operates on whole
// cache lines, so a buffer that doesn't start AND end on a line boundary
// would have that invalidate reach outside it and discard a dirty line
// belonging to unrelated data. Buffers not meeting this still work --
// they just silently take the slower byte-loop path instead of DMA.
// Declare DMA target buffers as:
//     static __attribute__((aligned(SPI3_DMA_BUFFER_ALIGNMENT))) uint8_t buf[N];
#define SPI3_DMA_BUFFER_ALIGNMENT   16u

// Shifts `length` bytes out from `txData` (or 0x00 filler if `txData` is
// NULL) while capturing the same number of bytes into `rxData` (discarded
// if `rxData` is NULL). Chip select is the caller's responsibility.
//
// When txData is NULL, rxData is non-NULL, length is in [32, 4096] bytes,
// and rxData/length both satisfy SPI3_DMA_BUFFER_ALIGNMENT, this runs over
// DMA (DCH2/DCH3, see spi3.c) instead of a SPI3_TransferByte() loop,
// freeing the CPU from shuttling each byte through SPI3BUF by hand.
// BLOCKING either way -- it returns only once the whole transfer has
// completed (interrupts stay enabled throughout, so ISRs are serviced
// normally, but no other main-loop work runs). Implemented on top of the
// async API below, so both paths share the same arm/finalize code.
void SPI3_TransferBlock(const uint8_t *txData, uint8_t *rxData, size_t length);

// --- Asynchronous (non-blocking) bulk receive -------------------------
//
// Starts a DMA capture of `length` bytes from SPI3 into `rxData` and
// returns IMMEDIATELY, without waiting for it to finish. The caller then
// polls SPI3_TransferIsBusy() and reads SPI3_TransferGetResult() once it
// goes false. Returns false (nothing started, nothing to poll) if another
// transfer is already in flight or if rxData/length don't meet the DMA
// eligibility rules described on SPI3_TransferBlock() above.
//
// `rxData` must stay valid and untouched by the caller until
// SPI3_TransferIsBusy() returns false -- the DMA engine is writing it in
// the background. The buffer is only safe to READ after that point:
// SPI3_TransferIsBusy() performs the D-cache invalidate as part of
// observing completion, so a caller that reads the buffer without
// polling to completion may see stale cached bytes.
//
// Chip select is still the caller's responsibility, and now spans the
// whole asynchronous window -- whatever asserted CS must keep it asserted
// until completion is observed. See SST25VF080B_ReadAsync()
// (spi/device_driver/sst25vf080b.h) for a device-level wrapper that owns
// that sequencing.
//
// CAUTION: an in-flight transfer is exposed to anything that touches the
// DMA controller globally. core/rtcc.c's rtccLock()/rtccUnlock() assert
// DMACONbits.SUSPEND and spin on DMABUSY -- harmless today because
// blocking transfers can never interleave with main-loop code, but a
// caller that starts an async transfer and then runs RTCC writes before
// it completes is a new interaction that has never been exercised.
bool SPI3_TransferBlockAsync(uint8_t *rxData, size_t length);

// True while an async transfer started by SPI3_TransferBlockAsync() is
// still running. Poll this to completion: the transition to false is what
// tears the DMA channels down, applies the D-cache invalidate to the
// receive buffer, and latches the result for SPI3_TransferGetResult().
// Also enforces the transfer timeout, so a wedged transfer eventually
// reports completion-with-failure instead of polling forever.
bool SPI3_TransferIsBusy(void);

// Result of the most recently completed async transfer: true if it moved
// every byte without a DMA error or timeout. Meaningful only once
// SPI3_TransferIsBusy() has returned false; returns false while a
// transfer is still in flight.
bool SPI3_TransferGetResult(void);

// Returns the SPI3 bus clock speed (Hz) actually produced by the current
// SPI3BRG setting, computed from the peripheral clock feeding SPI3.
uint32_t SPI3_GetBusSpeed(void);

// Prints SPI3 controller/status register state and calculated bus speed to
// the terminal.
void SPI3_PrintStatus(void);

// SPI3 DMA (DCH2 TX / DCH3 RX) interrupt service routines -- only defined
// in spi3.c (and their vectors only registered by SPI3_Initialize()) when
// SPI3_DMA_ENABLED is set there. Each just latches CHBCIF/CHERIF into a
// software flag SPI3_TransferBlock()'s DMA path waits on, then clears the
// channel's own interrupt-status bits and the CPU interrupt flag -- no
// SPI/flash protocol logic runs here, matching sdhcISR()'s minimal-ISR
// convention (sdhc/sdhc.h).
void __ISR(_DMA2_VECTOR, IPL1SRS) spi3TxDmaISR(void);
void __ISR(_DMA3_VECTOR, IPL2SRS) spi3RxDmaISR(void);

#ifdef __cplusplus
}
#endif

#endif /* SPI3_H */
