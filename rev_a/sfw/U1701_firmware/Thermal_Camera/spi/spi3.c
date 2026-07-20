/*******************************************************************************
  SPI3 Master Driver

  File Name:
    spi3.c

  Summary:
    Blocking driver for the SPI3 peripheral in master mode. See spi3.h for
    why this has no queue (unlike i2c_master.h) and no chip-select handling.
*******************************************************************************/

#include "spi/spi3.h"
#include "core/device_control.h"
#include "core/32mzda_interrupt_control.h"
#include "usb_uart/terminal_control.h"
#include <xc.h>
#include <sys/attribs.h>
#include <sys/kmem.h>

#include <stdio.h>

// SPI3 is clocked from PBCLK2, which PBCLK2Initialize() (device_control.c)
// divides down from SYSCLK by 3 (66.67 MHz on this board) -- same source as
// I2C1 (see I2C_PBCLK_HZ in i2c_master.c).
#define SPI3_PBCLK_HZ                (SYSCLK_INT / 3u)

// sst25vf080b.c reads with Fast Read (0Bh), not plain Read (03h), so every
// instruction this driver issues tolerates the SST25VF080B's full clock
// range (66/80 MHz per its datasheet Table 7-1) -- this bound reflects the
// PART's rating, not what's been proven reliable on THIS board.
#define SPI3_MAX_CLK_HZ              66000000UL

// REGRESSION 2026-07-20: raising this to the PBCLK2/2 (~33.3MHz) ceiling
// broke JEDEC ID readback on real hardware (SST25VF080B_Verify() failed,
// "SPI Flash FAILED to initialize" at boot) -- that path is plain
// SPI3_TransferByte(), untouched by the Fast Read change, so the clock
// increase itself is the cause, most likely board-level signal integrity
// (trace length/no termination) rather than anything the part's own
// datasheet rating would predict. Reverted to the original, long-proven
// 10MHz default. If you want to try raising this again, step it up
// incrementally on the bench with a scope on SCK/SDI rather than jumping
// straight to the calculated ceiling.
#define SPI3_DEFAULT_CLK_HZ          10000000UL

bool SPI3_TransferSetup(SPI3_TRANSFER_SETUP *setup, uint32_t srcClkFreq)
{
    uint32_t baudValue;
    uint32_t spiClkSpeed;

    if (setup == NULL)
    {
        return false;
    }

    spiClkSpeed = setup->clkSpeed;

    if ((spiClkSpeed == 0) || (spiClkSpeed > SPI3_MAX_CLK_HZ))
    {
        return false;
    }

    if (srcClkFreq == 0)
    {
        srcClkFreq = SPI3_PBCLK_HZ;
    }

    // Fsck = PBCLK / (2 * (BRG + 1))  =>  BRG = PBCLK / (2 * Fsck) - 1
    baudValue = (srcClkFreq / (2u * spiClkSpeed)) - 1u;

    if (baudValue > 0x1FFu)
    {
        return false;
    }

    SPI3BRG = baudValue;

    return true;
}

bool SPI3_Initialize(void)
{
    SPI3_TRANSFER_SETUP setup = { SPI3_DEFAULT_CLK_HZ };

    /* Disable the module while it's (re)configured */
    SPI3CONbits.ON = 0;

    /* Clear the receive buffer / overflow flag left over from any prior state */
    SPI3STATbits.SPIROV = 0;
    (void)SPI3BUF;

    if (!SPI3_TransferSetup(&setup, 0))
    {
        return false;
    }

    SPI3CONbits.SIDL = 0;      // continue running in CPU Idle mode
    SPI3CONbits.MSTEN = 1;     // master mode

    // SPI Mode 0 (CPOL=0, CPHA=0), which the SST25VF080B supports (along
    // with Mode 3). PIC32's CKE is inverted relative to standard CPHA:
    // CKP=0/CKE=1 here is Mode 0, not Mode 1.
    SPI3CONbits.CKP = 0;
    SPI3CONbits.CKE = 1;
    SPI3CONbits.SMP = 0;       // sample input at the middle of the data output time

    SPI3CONbits.MODE32 = 0;    // 8-bit word size (both MODE32 and MODE16 clear)
    SPI3CONbits.MODE16 = 0;

    SPI3CONbits.SSEN = 0;      // chip select is a plain GPIO owned by the device
    SPI3CONbits.MSSEN = 0;     // driver (nFLASH_SPI_CS_PIN), not the SPI module

    SPI3CONbits.ENHBUF = 0;    // legacy (non-FIFO) buffer mode -- polled via SPIRBF/SPITBE
    SPI3CONbits.DISSDO = 0;
    SPI3CONbits.DISSDI = 0;
    SPI3CONbits.MCLKSEL = 0;   // clock source is PBCLK (not REFCLK)
    SPI3CONbits.FRMEN = 0;     // framed sync mode disabled

    SPI3CONbits.ON = 1;

    return (SPI3CONbits.ON == 1);
}

uint8_t SPI3_TransferByte(uint8_t data)
{
    /* Wait for the transmit buffer to be free before loading the next byte */
    while (!SPI3STATbits.SPITBE);

    SPI3BUF = data;

    /* Wait for the byte shifted in during that same transfer to arrive */
    while (!SPI3STATbits.SPIRBF);

    return (uint8_t)SPI3BUF;
}

// --- DMA-accelerated bulk transfer -----------------------------------
//
// DCH2 (TX) and DCH3 (RX) -- DCH0/DCH1 are already claimed by
// usb_uart.c's console DMA (see its file header). Neither channel here
// raises a CPU interrupt: SPI3_TransferBlockDMA() busy-waits on the RX
// channel's own CHBCIF completion flag instead of registering a DMA ISR,
// which keeps this addition small and avoids adding two new interrupt
// vectors for a driver whose only caller (SST25VF080B_Read(), via
// flash_fileio.c) already calls it synchronously and has nothing useful
// to do while waiting. The CPU-side win is real anyway: the DMA engine
// shuttles every byte between SPI3BUF and RAM in hardware, instead of
// this driver's SPI3_TransferByte() loop doing it one SPIBUF load/store
// and two status-bit polls at a time.
#define SPI3_TX_DMA_CON_BITFIELD    DCH2CONbits
#define SPI3_TX_DMA_ECON_BITFIELD   DCH2ECONbits
#define SPI3_TX_DMA_INT_BITFIELD    DCH2INTbits
#define SPI3_TX_DMA_INTCLR_REG      DCH2INTCLR
#define SPI3_TX_DMA_SSA_REG         DCH2SSA
#define SPI3_TX_DMA_DSA_REG         DCH2DSA
#define SPI3_TX_DMA_SSIZ_REG        DCH2SSIZ
#define SPI3_TX_DMA_DSIZ_REG        DCH2DSIZ
#define SPI3_TX_DMA_CSIZ_REG        DCH2CSIZ

#define SPI3_RX_DMA_CON_BITFIELD    DCH3CONbits
#define SPI3_RX_DMA_ECON_BITFIELD   DCH3ECONbits
#define SPI3_RX_DMA_INT_BITFIELD    DCH3INTbits
#define SPI3_RX_DMA_INTCLR_REG      DCH3INTCLR
#define SPI3_RX_DMA_SSA_REG         DCH3SSA
#define SPI3_RX_DMA_DSA_REG         DCH3DSA
#define SPI3_RX_DMA_SSIZ_REG        DCH3SSIZ
#define SPI3_RX_DMA_DSIZ_REG        DCH3DSIZ
#define SPI3_RX_DMA_CSIZ_REG        DCH3CSIZ

// Disabled by default (2026-07-20): the SDHC ADMA2 path added in this same
// session hung real hardware the first time it was exercised despite
// looking correct on review (see sd_card.c's useDMA comment) -- this SPI3
// DMA path is equally unvalidated on real hardware, so it's left wired up
// but inert rather than risking the same kind of regression. Flip to 1
// only after bench-testing SPI3_TransferBlockDMA() in isolation (e.g. via
// "Flash Self Test", scoping SDO3/SDI3/SCK3 during a 4KB sector read).
#define SPI3_DMA_ENABLED   0

// Below this length, DMA channel setup/teardown overhead exceeds
// whatever it would save over the plain byte loop -- SPI3_TransferBlock()
// only takes the DMA path at or above this many bytes.
#define SPI3_DMA_MIN_LENGTH   32u

// Largest single transfer this driver's only bulk caller ever issues:
// SST25VF080B_Read() reading one 4KB NOR flash sector at a time
// (flash_fileio.c's page-at-a-time disk I/O). Sized to that specific,
// known ceiling, not a generic maximum -- see spi3_dma_tx_dummy[] below.
#define SPI3_DMA_MAX_LENGTH   4096u

// All-zero filler the TX channel feeds into SPI3BUF while the RX channel
// captures real data into the caller's buffer. SPI is full-duplex, so
// *something* has to go out on SDO3 to generate the clock edges that
// shift bytes in on SDI3 -- this driver's only DMA use case (a flash
// read) doesn't care what that content is, only the SST25VF080B's
// response on SDI3 matters. __attribute__((coherent)) matches this
// project's other DMA source-buffer convention (usb_uart.c's TX/RX
// buffers, sdhc.c's ADMA2 descriptor table); this content never changes
// after zero-init, so coherency here is about not having to think about
// cache state rather than an active hazard.
#if SPI3_DMA_ENABLED
static __attribute__((coherent)) uint8_t spi3_dma_tx_dummy[SPI3_DMA_MAX_LENGTH];

#define SPI3_DMA_TIMEOUT_TICKS   ((uint32_t)(((uint64_t)SYSCLK_INT / 2u) * 50000u / 1000000u))  // 50ms

// Captures `length` bytes (>= SPI3_DMA_MIN_LENGTH, <= SPI3_DMA_MAX_LENGTH)
// from SPI3BUF into `rxData` via DCH2/DCH3, transmitting spi3_dma_tx_dummy
// as filler. Returns false on a DMA timeout (SPI3_TransferBlock() falls
// back to the byte loop in that case, so this never leaves the caller
// without a correct, if slower, result).
static bool SPI3_TransferBlockDMA(uint8_t *rxData, size_t length)
{
    DMACONbits.ON = 1;

    // RX: SPI3BUF (fixed, 1 byte) -> rxData (grows to `length` bytes)
    SPI3_RX_DMA_CON_BITFIELD.CHEN = 0;
    SPI3_RX_DMA_CON_BITFIELD.CHPRI = 2;
    SPI3_RX_DMA_CON_BITFIELD.CHCHN = 0;
    SPI3_RX_DMA_ECON_BITFIELD.CHSIRQ = spi3_receive_done;
    SPI3_RX_DMA_ECON_BITFIELD.SIRQEN = 1;
    SPI3_RX_DMA_ECON_BITFIELD.PATEN = 0;
    SPI3_RX_DMA_SSA_REG = (uint32_t)KVA_TO_PA((void *)&SPI3BUF);
    SPI3_RX_DMA_DSA_REG = (uint32_t)KVA_TO_PA((void *)rxData);
    SPI3_RX_DMA_SSIZ_REG = 1;
    SPI3_RX_DMA_DSIZ_REG = (uint32_t)length;
    SPI3_RX_DMA_CSIZ_REG = 1;
    SPI3_RX_DMA_INTCLR_REG = 0x000000FFu;
    SPI3_RX_DMA_INT_BITFIELD.CHBCIF = 0;
    SPI3_RX_DMA_CON_BITFIELD.CHEN = 1;

    // TX: spi3_dma_tx_dummy (grows to `length` bytes) -> SPI3BUF (fixed)
    SPI3_TX_DMA_CON_BITFIELD.CHEN = 0;
    SPI3_TX_DMA_CON_BITFIELD.CHPRI = 2;
    SPI3_TX_DMA_CON_BITFIELD.CHCHN = 0;
    SPI3_TX_DMA_ECON_BITFIELD.CHSIRQ = spi3_transfer_done;
    SPI3_TX_DMA_ECON_BITFIELD.SIRQEN = 1;
    SPI3_TX_DMA_ECON_BITFIELD.PATEN = 0;
    SPI3_TX_DMA_SSA_REG = (uint32_t)KVA_TO_PA((void *)&spi3_dma_tx_dummy[0]);
    SPI3_TX_DMA_DSA_REG = (uint32_t)KVA_TO_PA((void *)&SPI3BUF);
    SPI3_TX_DMA_SSIZ_REG = (uint32_t)length;
    SPI3_TX_DMA_DSIZ_REG = 1;
    SPI3_TX_DMA_CSIZ_REG = 1;
    SPI3_TX_DMA_INTCLR_REG = 0x000000FFu;
    SPI3_TX_DMA_INT_BITFIELD.CHBCIF = 0;
    SPI3_TX_DMA_CON_BITFIELD.CHEN = 1;

    // SPITBE already reads set at idle (nothing queued yet), so the TX
    // channel's own trigger interrupt won't naturally edge for the very
    // first byte -- force it, same as usb_uart.c's TX DMA kick. Every
    // byte after that is paced by genuine SPITBE/SPIRBF edges as the
    // hardware shifts each byte in and out.
    SPI3_TX_DMA_ECON_BITFIELD.CFORCE = 1;

    uint32_t start = _CP0_GET_COUNT();
    bool timedOut = true;
    while ((uint32_t)(_CP0_GET_COUNT() - start) < SPI3_DMA_TIMEOUT_TICKS)
    {
        if (SPI3_RX_DMA_INT_BITFIELD.CHBCIF)
        {
            timedOut = false;
            break;
        }
    }

    SPI3_TX_DMA_CON_BITFIELD.CHEN = 0;
    SPI3_RX_DMA_CON_BITFIELD.CHEN = 0;

    return !timedOut;
}
#endif /* SPI3_DMA_ENABLED */

void SPI3_TransferBlock(const uint8_t *txData, uint8_t *rxData, size_t length)
{
    // DMA fast path: this driver's only bulk-transfer caller always
    // passes txData=NULL (it only wants the RX data; see
    // spi3_dma_tx_dummy[] for why that's what makes the DMA path
    // tractable without a real TX payload buffer). Falls back to the
    // byte loop for anything else -- arbitrary txData, no rxData, a
    // length outside the DMA path's sized/worthwhile range, or a DMA
    // timeout -- none of which this codebase currently exercises except
    // the size bounds.
#if SPI3_DMA_ENABLED
    if ((txData == NULL) && (rxData != NULL)
            && (length >= SPI3_DMA_MIN_LENGTH) && (length <= SPI3_DMA_MAX_LENGTH))
    {
        if (SPI3_TransferBlockDMA(rxData, length))
        {
            return;
        }
    }
#endif

    size_t i;

    for (i = 0; i < length; i++)
    {
        uint8_t rx = SPI3_TransferByte((txData != NULL) ? txData[i] : 0x00u);

        if (rxData != NULL)
        {
            rxData[i] = rx;
        }
    }
}

uint32_t SPI3_GetBusSpeed(void)
{
    return SPI3_PBCLK_HZ / (2u * ((uint32_t)SPI3BRG + 1u));
}

void SPI3_PrintStatus(void)
{
    uint32_t busSpeed = SPI3_GetBusSpeed();

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    --- SPI3 Controller ---\n\r");

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    PBCLK2 (SPI3 peripheral clock): %lu Hz\n\r", (unsigned long)SPI3_PBCLK_HZ);
    printf("    SPI3BRG: 0x%04X (%u)\n\r", (unsigned int)SPI3BRG, (unsigned int)SPI3BRG);
    printf("    Calculated bus speed: %lu Hz\n\r", (unsigned long)busSpeed);

    if (SPI3CONbits.ON) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    SPI3 Module is %s\n\r", SPI3CONbits.ON ? "enabled" : "disabled");

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Master mode: %s\n\r", SPI3CONbits.MSTEN ? "enabled" : "disabled");
    printf("    Clock Polarity (CKP): idle %s\n\r", SPI3CONbits.CKP ? "high" : "low");
    printf("    Clock Edge (CKE): output changes on %s edge\n\r",
           SPI3CONbits.CKE ? "active-to-idle" : "idle-to-active");
    printf("    Sample Phase (SMP): input sampled at %s of output time\n\r",
           SPI3CONbits.SMP ? "end" : "middle");

    if (SPI3STATbits.SPIROV) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Receive overflow has %s\n\r", SPI3STATbits.SPIROV ? "occurred" : "not occurred");

    if (SPI3STATbits.SPIBUSY) terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Bus is currently: %s\n\r", SPI3STATbits.SPIBUSY ? "busy" : "idle");

    terminalTextAttributesReset();
}
