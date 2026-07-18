/*******************************************************************************
  SDHC Host Controller Driver

  File Name:
    sdhc.c

  Summary:
    Register-level driver for the on-die SDHC peripheral. See sdhc.h for
    the layering rationale and the clock/card-detect design notes.
*******************************************************************************/

#include <xc.h>
#include <stdio.h>
#include <sys/kmem.h>

#include "sdhc/sdhc.h"
#include "core/device_control.h"
#include "core/32mzda_interrupt_control.h"
#include "gpio/pin_macros.h"
#include "usb_uart/terminal_control.h"

// Busy-waits until `cond` (a simple bitfield read, no side effects -- it's
// re-evaluated every iteration) becomes true or `timeoutTicks` CP0 Count
// ticks elapse. Mirrors SST25VF080B_WaitWhileBusy()'s polling style
// (spi/device_driver/sst25vf080b.c) generalized into a macro since SDHC
// bring-up needs this same pattern against many different status bits.
#define SDHC_WAIT_OR_TIMEOUT(cond, timeoutTicks, timedOutFlag) \
    do { \
        uint32_t _sdhc_wait_start = _CP0_GET_COUNT(); \
        (timedOutFlag) = true; \
        do { \
            if (cond) { (timedOutFlag) = false; break; } \
        } while ((uint32_t)(_CP0_GET_COUNT() - _sdhc_wait_start) < (uint32_t)(timeoutTicks)); \
    } while (0)

#define SDHC_TIMEOUT_TICKS(us)      ((uint32_t)(((uint64_t)SYSCLK_INT / 2u) * (us) / 1000000u))
#define SDHC_CLOCK_TIMEOUT_TICKS    SDHC_TIMEOUT_TICKS(10000u)    // 10ms for ICLKSTABLE
#define SDHC_RESET_TIMEOUT_TICKS    SDHC_TIMEOUT_TICKS(100000u)   // 100ms for SWRALL self-clear
#define SDHC_CMD_TIMEOUT_TICKS      SDHC_TIMEOUT_TICKS(1000u)     // 1ms for command inhibit / command complete
#define SDHC_DATA_TIMEOUT_TICKS     SDHC_TIMEOUT_TICKS(500000u)   // 500ms for a block transfer

// SD spec: identification-phase clock must not exceed 400kHz. Operating
// speed below is the "Default Speed" grade (25MHz max); High-Speed
// (50MHz, gated on SDHCCAP.HISPEED and the card's own CMD6-reported
// support) is a phase-2 item, not implemented here -- see sdhc.h.
#define SDHC_IDENTIFICATION_CLOCK_HZ   400000UL
#define SDHC_DEFAULT_SPEED_CLOCK_HZ    25000000UL

static uint32_t sdhc_base_clock_hz = 0;
static bool     sdhc_adma2_supported = false;

// Set by SDHC_ConfigureBlockTransfer(), consumed by the next
// SDHC_SendCommand(..., dataPresent=true) call to build the Transfer Mode
// bits of the combined SDHCMODE write.
static uint8_t sdhc_pending_dtxdsel = 0; // 0 = write (host->card), 1 = read (card->host)
static uint8_t sdhc_pending_dmaen   = 0;
static uint8_t sdhc_pending_bsel    = 0;
static uint8_t sdhc_pending_bcen    = 0;

bool SDHC_Initialize(void)
{
    bool timedOut;

    disableInterrupt(sdhc_interrupt);

    // CFGCON2.SDCDEN/SDWPEN are assumed left at their POR default of 0
    // ("available for general purpose use") -- this driver does not touch
    // CFGCON2 at all, since it bundles other unrelated peripheral config
    // bits (SDRDFTHR/SDWRFTHR FIFO thresholds, SDWPPOL) that shouldn't be
    // disturbed here. Card detect is read as a plain GPIO
    // (SD_CARD_DETECT_PIN, sd_card.c) instead of the SDHC peripheral's own
    // dedicated CD pin logic -- see sdhc.h file header for why.

    // Full software reset (command line, data line, and register state)
    // FIRST, then bring up the internal clock -- SWRALL wipes SDHCCON2
    // including ICLKEN, so enabling the clock before the reset (as a
    // first draft of this driver did) leaves the module clockless the
    // moment SWRALL lands. Matches Harmony plib_sdhc's ordering.
    SDHCCON2bits.SWRALL = 1;
    SDHC_WAIT_OR_TIMEOUT(!SDHCCON2bits.SWRALL, SDHC_RESET_TIMEOUT_TICKS, timedOut);
    if (timedOut)
    {
        return false;
    }

    // Enable the internal clock generator and wait for it to stabilize
    // before touching anything else (required sequencing per SDHCI spec)
    SDHCCON2bits.SDCLKEN = 0;
    SDHCCON2bits.ICLKEN = 1;
    SDHC_WAIT_OR_TIMEOUT(SDHCCON2bits.ICLKSTABLE, SDHC_CLOCK_TIMEOUT_TICKS, timedOut);
    if (timedOut)
    {
        return false;
    }

    // Data timeout counter -- conservative, near-maximum value. The exact
    // DTOC-to-time formula (TOCLKFREQ-relative per SDHCCAP) is not
    // re-derived here; this is a "generous enough not to false-trip
    // during a normal transfer" choice, not a precisely calculated one.
    // Revisit if legitimate transfers are seen timing out against DTOEIF.
    SDHCCON2bits.DTOC = 0xEu;

    // Cache capabilities -- SDHC_SetClockDivider() (called below) and
    // SDHC_IsADMA2Supported() both depend on this being read first
    sdhc_base_clock_hz = (uint32_t)SDHCCAPbits.BASECLK * 1000000UL;
    sdhc_adma2_supported = (SDHCCAPbits.ADMA2 != 0);

    if (sdhc_base_clock_hz == 0)
    {
        // SDHCI spec allows BASECLK=0 to mean "must be obtained by other
        // means" on some implementations; this driver has no fallback
        // source for it, so treat it as an init failure rather than
        // silently dividing by zero later.
        return false;
    }

    // 1-bit bus width, Default Speed (not High-Speed) until the card
    // identification sequence (sd_card.c) has negotiated otherwise
    SDHCCON1bits.DTXWIDTH = 0;
    SDHCCON1bits.HSEN = 0;

    // SD bus power -- this bit is the SDHCI-standard "turn on card VDD"
    // control, but on this board the card's actual power comes from the
    // external load switch (SD_PWR_EN_PIN, gpio/pin_macros.h RG15), not
    // from an internal SDHC-controlled regulator. Setting it anyway in
    // case this implementation gates CINHCMD/CINHDAT behavior on it.
    SDHCCON1bits.SDBP = 1;

    if (!SDHC_SetClockDivider(SDHC_IDENTIFICATION_CLOCK_HZ))
    {
        return false;
    }

    // Enable command-complete, transfer-complete, and every error flag at
    // the peripheral level (Flag Enable) so SDHCINTSTAT bits actually get
    // set for SDHC_SendCommand()/SDHC_TransferBlocks*()'s blocking polls
    // to observe. Signal Enable (and the CPU-level interrupt) is
    // deliberately left OFF: enabling it raced sdhcISR() against those
    // same polling loops -- CCIF fires ~120us after a command at the
    // 400kHz identification clock, well inside the loops' own timeout
    // window, so the (IPL2, higher-priority) ISR almost always clears
    // SDHCINTSTAT via its own W1C before the foreground poll's follow-up
    // reads run, making genuinely-successful commands (e.g. CMD0) read
    // back as CCIF=0/EIF=0 and fail. sdhcISR() is kept for when this
    // driver grows an actual async use (e.g. low-power wake-on-card-event
    // via SDHCCON1.WKONINT/WKONINS) -- re-enable SDHCINTSEN/enableInterrupt
    // together only once nothing is concurrently polling the same flags.
    SDHCINTEN = 0x03FF8003u;   // CCIE, TXCIE, EIE, and all *EIE error bits

    return true;
}

bool SDHC_SetClockDivider(uint32_t targetHz)
{
    bool timedOut;

    if (sdhc_base_clock_hz == 0 || targetHz == 0)
    {
        return false;
    }

    // Despite the ATDF documenting only the SDHCI-Ver2.00-style 8-bit
    // SDCLKDIV field, this implementation takes an SDHCI-Ver3.00-style
    // *arbitrary* 10-bit divisor: SDCLK = BaseClock / (2*N) for N != 0,
    // or BaseClock / 1 (bypass) for N == 0, with N's low 8 bits in
    // SDCLKDIV<7:0> (SDHCCON2<15:8>) and its high 2 bits in the
    // ATDF-undocumented SDHCCON2<7:6>. This matches Microchip's own
    // Harmony plib_sdhc for the PIC32MZ DA (which programs N =
    // BaseClock/target/2, e.g. N=250 for the 400kHz identification clock
    // from the 200MHz REFCLK4 base) -- a one-hot power-of-two divider
    // could never reach 400kHz from 200MHz (200MHz/256 = 781kHz).
    // Round N up so SDCLK lands at or below targetHz, never above it.
    uint32_t n = 0;
    if (targetHz < sdhc_base_clock_hz)
    {
        n = (sdhc_base_clock_hz + (2u * targetHz) - 1u) / (2u * targetHz);
        if (n > 0x3FFu)
        {
            n = 0x3FFu;
        }
    }

    // Gate SDCLK off before reprogramming the divider, per required
    // sequencing, then back on
    SDHCCON2bits.SDCLKEN = 0;
    SDHCCON2 = (SDHCCON2 & ~(_SDHCCON2_SDCLKDIV_MASK | 0x000000C0u))
             | ((n & 0xFFu) << _SDHCCON2_SDCLKDIV_POSITION)
             | (((n >> 8) & 0x3u) << 6);
    SDHCCON2bits.SDCLKEN = 1;

    SDHC_WAIT_OR_TIMEOUT(SDHCCON2bits.ICLKSTABLE, SDHC_CLOCK_TIMEOUT_TICKS, timedOut);
    return !timedOut;
}

bool SDHC_SetBusWidth(bool wide4bit)
{
    SDHCCON1bits.DTXWIDTH = wide4bit ? 1u : 0u;
    return true;
}

void SDHC_ConfigureBlockTransfer(uint16_t blockSize, uint16_t blockCount, bool isWrite, bool useADMA2)
{
    SDHCBLKCONbits.BSIZE = blockSize;
    SDHCBLKCONbits.BCOUNT = blockCount;

    sdhc_pending_dtxdsel = isWrite ? 0u : 1u;
    sdhc_pending_dmaen = (useADMA2 && sdhc_adma2_supported) ? 1u : 0u;
    sdhc_pending_bsel = (blockCount > 1u) ? 1u : 0u;
    sdhc_pending_bcen = (blockCount > 1u) ? 1u : 0u;
}

// Recovers the CMD/DATA line inhibit state machine after a command
// error. Per the SDHCI spec, CINHCMD/CINHDAT can latch stuck after a
// command timeout/CRC/end-bit/index error until an explicit
// SWRCMD/SWRDATA software reset runs -- there's no automatic recovery.
// Without this, every SDHC_SendCommand() call after the first failure
// dies silently on this function's own initial CINHCMD wait, before
// ever issuing anything (this is what made CMD55/ACMD41 fail with no
// diagnostic output after a CMD8 timeout during bring-up).
static bool SDHC_ResetCommandLine(void)
{
    bool timedOut;
    SDHCCON2bits.SWRCMD = 1;
    SDHC_WAIT_OR_TIMEOUT(!SDHCCON2bits.SWRCMD, SDHC_RESET_TIMEOUT_TICKS, timedOut);
    return !timedOut;
}

static bool SDHC_ResetDataLine(void)
{
    bool timedOut;
    SDHCCON2bits.SWRDATA = 1;
    SDHC_WAIT_OR_TIMEOUT(!SDHCCON2bits.SWRDATA, SDHC_RESET_TIMEOUT_TICKS, timedOut);
    return !timedOut;
}

bool SDHC_SendCommand(uint8_t cmdIndex, uint32_t argument, sdhc_response_type_t responseType, bool dataPresent)
{
    bool timedOut;

    SDHC_WAIT_OR_TIMEOUT(!SDHCSTAT1bits.CINHCMD, SDHC_CMD_TIMEOUT_TICKS, timedOut);
    if (timedOut)
    {
        // CINHCMD stuck from a prior command's error -- reset the CMD
        // line and give this attempt one more chance before giving up.
        printf("    SDHC_SendCommand: CMD%u found CINHCMD stuck, resetting CMD line\r\n",
                (unsigned)cmdIndex);
        SDHC_ResetCommandLine();
        SDHC_WAIT_OR_TIMEOUT(!SDHCSTAT1bits.CINHCMD, SDHC_CMD_TIMEOUT_TICKS, timedOut);
        if (timedOut)
        {
            return false;
        }
    }

    if (dataPresent)
    {
        SDHC_WAIT_OR_TIMEOUT(!SDHCSTAT1bits.CINHDAT, SDHC_CMD_TIMEOUT_TICKS, timedOut);
        if (timedOut)
        {
            printf("    SDHC_SendCommand: CMD%u found CINHDAT stuck, resetting DATA line\r\n",
                    (unsigned)cmdIndex);
            SDHC_ResetDataLine();
            SDHC_WAIT_OR_TIMEOUT(!SDHCSTAT1bits.CINHDAT, SDHC_CMD_TIMEOUT_TICKS, timedOut);
            if (timedOut)
            {
                return false;
            }
        }
    }

    // Clear any leftover status flags from a prior command so the
    // CCIF/EIF poll below can only see completion/errors belonging to
    // THIS command (W1C: writing back the set bits clears them)
    SDHCINTSTAT = SDHCINTSTAT;

    SDHCARG = argument;

    uint8_t respTypeBits, ccrccen, cidxcen;
    switch (responseType)
    {
        case SDHC_RESP_R2:
            respTypeBits = 0x1u; ccrccen = 1u; cidxcen = 0u;
            break;
        case SDHC_RESP_R3:
            respTypeBits = 0x2u; ccrccen = 0u; cidxcen = 0u;
            break;
        case SDHC_RESP_R1B:
            respTypeBits = 0x3u; ccrccen = 1u; cidxcen = 1u;
            break;
        case SDHC_RESP_R1:
        case SDHC_RESP_R6R7:
            respTypeBits = 0x2u; ccrccen = 1u; cidxcen = 1u;
            break;
        case SDHC_RESP_NONE:
        default:
            respTypeBits = 0x0u; ccrccen = 0u; cidxcen = 0u;
            break;
    }

    // Compose the combined Transfer-Mode-and-Command register in a local
    // and store it with ONE 32-bit write -- on this implementation
    // SDHCMODE is one 32-bit register (unlike the split 16+16 pair in the
    // generic SDHCI spec) and a write containing the command field issues
    // the command. Field-by-field SDHCMODEbits assignments do NOT write
    // just one field: each is a full 32-bit read-modify-write whose first
    // store re-issues the previous command (stale CIDX/RESPTYPE), after
    // which the remaining stores land while CINHCMD=1 -- undefined per
    // SDHCI, and on this silicon the real command never goes out cleanly.
    // This was the 2026-07-17 bring-up failure: CMD0 (all-zero config, no
    // response) appeared to work, but CMD8 -- the first command with a
    // nonzero config -- died with CTOEIF because the card never received
    // a well-formed CMD8 to respond to.
    uint32_t mode =
          ((uint32_t)(dataPresent ? sdhc_pending_dmaen : 0u) << _SDHCMODE_DMAEN_POSITION)
        | ((uint32_t)(dataPresent ? sdhc_pending_bcen : 0u) << _SDHCMODE_BCEN_POSITION)
        // ACEN = 0 (Auto CMD12 not used), CTYPE = 0 (normal command)
        | ((uint32_t)(dataPresent ? sdhc_pending_dtxdsel : 0u) << _SDHCMODE_DTXDSEL_POSITION)
        | ((uint32_t)(dataPresent ? sdhc_pending_bsel : 0u) << _SDHCMODE_BSEL_POSITION)
        | ((uint32_t)respTypeBits << _SDHCMODE_RESPTYPE_POSITION)
        | ((uint32_t)ccrccen << _SDHCMODE_CCRCCEN_POSITION)
        | ((uint32_t)cidxcen << _SDHCMODE_CIDXCEN_POSITION)
        | ((uint32_t)(dataPresent ? 1u : 0u) << _SDHCMODE_DPSEL_POSITION)
        | ((uint32_t)cmdIndex << _SDHCMODE_CIDX_POSITION);
    SDHCMODE = mode;

    SDHC_WAIT_OR_TIMEOUT((SDHCINTSTATbits.CCIF || SDHCINTSTATbits.EIF), SDHC_CMD_TIMEOUT_TICKS, timedOut);

    bool cmdComplete = SDHCINTSTATbits.CCIF;
    bool errorFlag = SDHCINTSTATbits.EIF;
    bool cmdTimeoutErr = SDHCINTSTATbits.CTOEIF;
    bool cmdCrcErr = SDHCINTSTATbits.CCRCEIF;
    bool cmdEndBitErr = SDHCINTSTATbits.CEBEIF;
    bool cmdIdxErr = SDHCINTSTATbits.CIDXEIF;

    // Write-back-what-was-read is the standard write-1-to-clear idiom;
    // harmless no-op if SDHCINTSTAT genuinely is read-only as the ATDF's
    // rw="R" annotation claims (verify during bring-up if flags stick)
    SDHCINTSTAT = SDHCINTSTAT;

    bool success = !timedOut && cmdComplete && !errorFlag && !cmdTimeoutErr
            && !cmdCrcErr && !cmdEndBitErr && !cmdIdxErr;

    if (!success)
    {
        printf("    SDHC_SendCommand: CMD%u failed -- timedOut=%u CCIF=%u EIF=%u "
                "CTOEIF=%u CCRCEIF=%u CEBEIF=%u CIDXEIF=%u CINHCMD=%u\r\n",
                (unsigned)cmdIndex, (unsigned)timedOut, (unsigned)cmdComplete,
                (unsigned)errorFlag, (unsigned)cmdTimeoutErr, (unsigned)cmdCrcErr,
                (unsigned)cmdEndBitErr, (unsigned)cmdIdxErr,
                (unsigned)SDHCSTAT1bits.CINHCMD);

        // Leave the CMD line clean for whatever command comes next --
        // any of the error flags above (or the response-wait timing out)
        // can latch CINHCMD stuck otherwise (see SDHC_ResetCommandLine()).
        SDHC_ResetCommandLine();
        if (dataPresent)
        {
            SDHC_ResetDataLine();
        }
    }

    return success;
}

void SDHC_GetResponse(uint32_t response[4])
{
    if (response == NULL)
    {
        return;
    }

    response[0] = SDHCRESP0;
    response[1] = SDHCRESP1;
    response[2] = SDHCRESP2;
    response[3] = SDHCRESP3;
}

bool SDHC_TransferBlocksPIO(uint8_t *buffer, uint16_t blockSize, uint16_t blockCount, bool isWrite)
{
    // Buffer must be 4-byte aligned -- true of FatFs's internal sector
    // buffers on this platform, but not guaranteed for an arbitrary
    // caller-supplied pointer.
    uint32_t *buf32 = (uint32_t *)(void *)buffer;
    uint16_t wordsPerBlock = blockSize / 4u;
    uint16_t block, word;
    bool timedOut = false;

    for (block = 0; block < blockCount; block++)
    {
        if (isWrite)
        {
            SDHC_WAIT_OR_TIMEOUT(SDHCSTAT1bits.BWEN, SDHC_DATA_TIMEOUT_TICKS, timedOut);
        }
        else
        {
            SDHC_WAIT_OR_TIMEOUT(SDHCSTAT1bits.BREN, SDHC_DATA_TIMEOUT_TICKS, timedOut);
        }

        if (timedOut)
        {
            return false;
        }

        for (word = 0; word < wordsPerBlock; word++)
        {
            if (isWrite)
            {
                SDHCDATA = *buf32++;
            }
            else
            {
                *buf32++ = SDHCDATA;
            }
        }
    }

    SDHC_WAIT_OR_TIMEOUT(SDHCINTSTATbits.TXCIF, SDHC_DATA_TIMEOUT_TICKS, timedOut);

    bool errorFlag = SDHCINTSTATbits.EIF;
    SDHCINTSTAT = SDHCINTSTAT;

    return !timedOut && !errorFlag;
}

// Standard SD Host Controller Simplified Spec 3.00 32-bit ADMA2
// descriptor: a 16-bit attribute/length-adjacent field followed by a
// 32-bit data address. This is the near-universal SDHCI descriptor
// layout, but has NOT been confirmed against the PIC32MZ-DA Family
// Reference Manual's SDHC chapter for this specific silicon (see the
// implementation plan's Verification section) -- validate before relying
// on this path; SDHC_TransferBlocksPIO() is the confirmed fallback.
typedef struct __attribute__((packed))
{
    uint16_t attributes; // Valid(bit0), End(bit1), Int(bit2), Act(bits4:5)
    uint16_t length;     // transfer length in bytes (0 encodes 65536)
    uint32_t address;    // physical address of the data buffer
} sdhc_adma2_descriptor_t;

#define SDHC_ADMA2_ATTR_VALID  (1u << 0)
#define SDHC_ADMA2_ATTR_END    (1u << 1)
#define SDHC_ADMA2_ACT_TRAN    (2u << 4) // "Transfer Data" descriptor type

// Single-entry descriptor table -- sd_card.c's block r/w wrappers always
// pass one contiguous buffer, so one descriptor covers the whole
// transfer. __attribute__((coherent)) keeps this KSEG1-uncached from the
// ADMA2 engine's perspective with no manual cache maintenance, matching
// usb_uart.c's DMA buffer convention.
static __attribute__((coherent)) sdhc_adma2_descriptor_t sdhc_adma2_table[1];

bool SDHC_TransferBlocksADMA2(uint8_t *buffer, uint16_t blockSize, uint16_t blockCount, bool isWrite)
{
    (void)isWrite; // direction was already set via SDHC_ConfigureBlockTransfer()/SDHCMODE.DTXDSEL

    uint32_t totalBytes = (uint32_t)blockSize * (uint32_t)blockCount;

    if ((totalBytes == 0) || (totalBytes > 0x10000u))
    {
        // A single descriptor's 16-bit length field tops out at 65536
        // bytes (0 encodes 65536); larger transfers would need a multi-
        // descriptor chain, not implemented in this first pass.
        return false;
    }

    sdhc_adma2_table[0].attributes = SDHC_ADMA2_ATTR_VALID | SDHC_ADMA2_ATTR_END | SDHC_ADMA2_ACT_TRAN;
    sdhc_adma2_table[0].length = (totalBytes == 0x10000u) ? 0u : (uint16_t)totalBytes;
    sdhc_adma2_table[0].address = (uint32_t)KVA_TO_PA(buffer);

    SDHCAADDR = (uint32_t)KVA_TO_PA((void *)&sdhc_adma2_table[0]);

    bool timedOut;
    SDHC_WAIT_OR_TIMEOUT(SDHCINTSTATbits.TXCIF, SDHC_DATA_TIMEOUT_TICKS, timedOut);

    bool errorFlag = SDHCINTSTATbits.EIF;
    bool admaErr = SDHCINTSTATbits.ADEIF;
    SDHCINTSTAT = SDHCINTSTAT;

    return !timedOut && !errorFlag && !admaErr;
}

bool SDHC_IsADMA2Supported(void)
{
    return sdhc_adma2_supported;
}

bool SDHC_IsDataLineBusy(void)
{
    return (SDHCSTAT1bits.DLACTIVE || SDHCSTAT1bits.WRACTIVE || SDHCSTAT1bits.RDACTIVE) != 0;
}

uint32_t SDHC_GetBaseClockHz(void)
{
    return sdhc_base_clock_hz;
}

void __ISR(_SDHC_VECTOR, IPL2SRS) sdhcISR(void)
{
    // Normal command/data completion and errors are consumed by the
    // blocking polling loops in SDHC_SendCommand()/SDHC_TransferBlocks*()
    // above, which check SDHCINTSTAT directly rather than depending on
    // this ISR -- matching this codebase's existing blocking-driver
    // convention (spi3.c, sst25vf080b.c, ddr2.c). This handler exists so
    // the SDHC interrupt is safely serviced even if an event fires
    // outside a blocking wait (e.g. an asynchronous fault), rather than
    // left permanently pending. No SD-protocol logic runs here.
    SDHCINTSTAT = SDHCINTSTAT; // W1C idiom, see SDHC_SendCommand() comment
    clearInterruptFlag(sdhc_interrupt);
}

void SDHC_PrintStatus(void)
{
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    --- SDHC Controller ---\n\r");

    if (PMD6bits.SDHCMD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    PMD Gating: %s\n\r", PMD6bits.SDHCMD ? "disabled (SDHCMD=1)" : "enabled");

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Internal Clock: %s, Stable: %s\n\r",
            SDHCCON2bits.ICLKEN ? "enabled" : "disabled",
            SDHCCON2bits.ICLKSTABLE ? "yes" : "no");
    // 10-bit arbitrary divisor N: low 8 bits in SDCLKDIV, high 2 bits in
    // the ATDF-undocumented SDHCCON2<7:6> -- see SDHC_SetClockDivider()
    uint32_t sdclkdivN = (uint32_t)SDHCCON2bits.SDCLKDIV | (((SDHCCON2 >> 6) & 0x3u) << 8);
    printf("    SD Clock: %s, divisor N: %lu (SDCLK = BaseClock/(2*N))\n\r",
            SDHCCON2bits.SDCLKEN ? "enabled" : "disabled", (unsigned long)sdclkdivN);
    printf("    Base Clock (measured, SDHCCAP.BASECLK): %lu Hz\n\r", (unsigned long)sdhc_base_clock_hz);

    if (SDHCCON2bits.SDCLKEN && (sdclkdivN > 0))
    {
        printf("    Calculated SDCLK: %lu Hz\n\r",
                (unsigned long)(sdhc_base_clock_hz / (2u * sdclkdivN)));
    }
    else if (SDHCCON2bits.SDCLKEN)
    {
        printf("    Calculated SDCLK: %lu Hz (bypass, no divider)\n\r", (unsigned long)sdhc_base_clock_hz);
    }

    printf("    Bus Width: %s\n\r", SDHCCON1bits.DTXWIDTH ? "4-bit" : "1-bit");
    printf("    High-Speed Mode: %s\n\r", SDHCCON1bits.HSEN ? "enabled" : "disabled");
    printf("    ADMA2 Capable: %s\n\r", sdhc_adma2_supported ? "yes" : "no");
    printf("    DMA Select (SDHCCON1.DMASEL): %u\n\r", (unsigned int)SDHCCON1bits.DMASEL);

    printf("    SDHCCAP: TOCLKFREQ=%u BASECLK=%uMHz MBLEN=%u BUS8BIT=%u ADMA2=%u HISPEED=%u VOLT3V3=%u\n\r",
            (unsigned int)SDHCCAPbits.TOCLKFREQ, (unsigned int)SDHCCAPbits.BASECLK,
            (unsigned int)SDHCCAPbits.MBLEN, (unsigned int)SDHCCAPbits.BUS8BIT,
            (unsigned int)SDHCCAPbits.ADMA2, (unsigned int)SDHCCAPbits.HISPEED,
            (unsigned int)SDHCCAPbits.VOLT3V3);

    printf("    CPU Interrupt Enabled: %s\n\r", getInterruptEnable(sdhc_interrupt) ? "yes" : "no");

    terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    SDHC Peripheral CARDINS (unused, CFGCON2.SDCDEN left 0): %s\n\r", SDHCSTAT1bits.CARDINS ? "T" : "F");

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    SD_CARD_DETECT_PIN (RA0) raw level: %s\n\r", SD_CARD_DETECT_PIN ? "high" : "low");
    printf("    SD_PWR_EN_PIN (RG15): %s\n\r", SD_PWR_EN_PIN ? "high (card power enabled)" : "low (card power disabled)");

    terminalTextAttributesReset();
}
