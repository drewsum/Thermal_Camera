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
#include "usb_uart/terminal_control.h"
#include <xc.h>

#include <stdio.h>

// SPI3 is clocked from PBCLK2, which PBCLK2Initialize() (device_control.c)
// divides down from SYSCLK by 3 (66.67 MHz on this board) -- same source as
// I2C1 (see I2C_PBCLK_HZ in i2c_master.c).
#define SPI3_PBCLK_HZ                (SYSCLK_INT / 3u)

// The SST25VF080B's plain Read (03h) instruction -- the only read
// instruction this driver issues -- is speced up to 25 MHz max (its
// datasheet Table 7-1); other instructions tolerate the part's full
// 66/80 MHz range, so 25 MHz is the binding ceiling for this driver.
#define SPI3_MAX_CLK_HZ              25000000UL

// Conservative default well under the 25 MHz ceiling, for margin.
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

void SPI3_TransferBlock(const uint8_t *txData, uint8_t *rxData, size_t length)
{
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
