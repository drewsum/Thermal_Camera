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

// Shifts `length` bytes out from `txData` (or 0x00 filler if `txData` is
// NULL) while capturing the same number of bytes into `rxData` (discarded
// if `rxData` is NULL). Chip select is the caller's responsibility.
//
// When txData is NULL, rxData is non-NULL, and length is in [32, 4096]
// bytes, this runs over DMA (DCH2/DCH3, see spi3.c) instead of a
// SPI3_TransferByte() loop, freeing the CPU from shuttling each byte
// through SPI3BUF by hand. Still a blocking call either way -- it
// returns only once the whole transfer (DMA or byte loop) has completed.
void SPI3_TransferBlock(const uint8_t *txData, uint8_t *rxData, size_t length);

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
