/*******************************************************************************
  SDHC Host Controller Driver

  File Name:
    sdhc.h

  Summary:
    Register-level driver for the PIC32MZ2064DAR176's on-die SD Host
    Controller (SDHCI-Simplified-Spec-3.0-style register set at SDHCBLKCON..
    SDHCAADDR, base address 0xBF8EC000+, Family Reference Manual DS60001321).

  Description:
    This is the lowest layer of the SD card stack (sdhc.c -> sd_card.c ->
    fatfs/diskio.c -> FatFs). It knows nothing about SD command semantics
    (no CMD0/ACMD41 literals) -- it exposes generic command-index/argument/
    response-type/data-present primitives, block-transfer primitives (PIO
    and ADMA2), clock/interrupt bring-up, and a peripheral-level status
    printout. sd_card.c (device_driver/sd_card.h) is what actually knows
    what a CMD8 is.

    Clock: the SDHC base clock is REFCLK4 -- REFCLK4Initialize()
    (core/device_control.c) runs it at SYSCLK/1 = 200MHz, matching the
    frequency SDHCCAP.BASECLK advertises (in MHz, per SDHCI convention) and
    matching Microchip's Harmony clock config for PIC32MZ DA SDHC parts. If
    REFCLK4 is left disabled, SDHCCON2.ICLKSTABLE never sets and
    SDHC_Initialize() fails -- exactly the failure seen on first bring-up
    (2026-07-15), when a design note here wrongly claimed no REFCLK was
    wired to SDHC. SDHC_SetClockDivider() still reads BASECLK back at
    runtime and derives the divisor from it rather than hardcoding 200MHz
    -- this project has already been burned once by an incorrect *assumed*
    clock relationship (the DDR2 driver's MPLL-vs-DRAM-clock 2x error).

    Card-detect: CFGCON2.SDCDEN selects whether the chip's dedicated SDCD
    pin function is claimed by the SDHC peripheral itself (autonomous
    CARDINS/CDSLVL tracking) or left as a general-purpose pin. This driver
    leaves SDCDEN clear (0) -- SD_CARD_DETECT_PIN (gpio/pin_macros.h, RA0)
    is read as a plain polled GPIO by sd_card.c instead (see sd_card.h for
    why: RA0's change-notification interrupt is already owned by
    portAChangeNoticeISR() in application/pushbuttons.c, so this pin can't
    also get its own CN ISR without a conflict -- polling avoids that
    entirely and keeps the SDHC peripheral's own card-detect logic out of
    the way).

    Register field names below (SDHCMODE, SDHCCON1/2, SDHCSTAT1/2,
    SDHCINTSTAT/EN/SEN, SDHCCAP) were cross-checked field-by-field against
    the PIC32MZ2064DAR176 ATDF and match the XC32 v4.45 header exactly,
    with one cosmetic exception: SDHCCAP's SLOTTYPE field bit-packs past
    the register's 32-bit boundary in both the ATDF and header (a
    documented-but-unusable overflow) -- this driver does not read
    SLOTTYPE for that reason.
*******************************************************************************/

#ifndef SDHC_H
#define SDHC_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/attribs.h>

#ifdef __cplusplus
extern "C" {
#endif

// SD Host Controller response-format classes (SDHCI-level concepts, shared
// across SD/MMC/SDIO command sets -- not specific SD command indices).
typedef enum {
    SDHC_RESP_NONE = 0, // No response expected
    SDHC_RESP_R1,       // 48-bit, CRC + command-index checked (normal response)
    SDHC_RESP_R1B,      // 48-bit, CRC + index checked, card asserts busy on DAT0 after
    SDHC_RESP_R2,       // 136-bit, CRC checked, no index check (CID/CSD registers)
    SDHC_RESP_R3,       // 48-bit, no CRC/index check (OCR register)
    SDHC_RESP_R6R7      // 48-bit, CRC + index checked (RCA / interface condition)
} sdhc_response_type_t;

// Brings up the SDHC peripheral: assumes PMD6bits.SDHCMD == 0 already
// (application/power_saving.c), runs a full software reset (SWRALL --
// which wipes SDHCCON2, so it must come first), then enables the internal
// clock and waits for ICLKSTABLE, sets the card-
// identification clock rate (400kHz max per SD spec) via
// SDHC_SetClockDivider(), sets 1-bit bus width, registers and enables the
// SDHC interrupt (sdhc_interrupt / _SDHC_VECTOR = 191) through the
// project's generic interrupt API, and caches SDHCCAP for later use by
// SDHC_GetBaseClockHz()/SDHC_PrintStatus(). Every wait in here is CP0-
// Count timeout-bounded, never an unconditional spin. Returns false if the
// internal clock never stabilizes or the reset never clears.
bool SDHC_Initialize(void);

// Reprograms the SD clock divisor to produce an SDCLK at or below
// targetHz, using SDHC_GetBaseClockHz() (not a hardcoded assumption) as
// the reference. The divisor is an arbitrary 10-bit N (SDCLK =
// BaseClock/(2*N), N's high 2 bits in ATDF-undocumented SDHCCON2<7:6>),
// matching Harmony's plib_sdhc for this family -- NOT the one-hot
// power-of-two SDHCI Ver2.00 scheme, which could not reach the 400kHz
// identification rate from the 200MHz base clock. Gates SDCLKEN off
// before reprogramming and back on after, per the SDHCI spec's required
// sequencing, waiting ICLKSTABLE both times. Returns false on timeout.
bool SDHC_SetClockDivider(uint32_t targetHz);

// Gates SDCLK off (SDHCCON2.SDCLKEN = 0), leaving the internal clock and
// all other controller state alone. Called on card teardown
// (SD_Card_Deinitialize()) so the bus is QUIET while no card is mounted:
// a card hot-inserted into a slot with the previous session's 25MHz
// SDCLK free-running can clock in contact-bounce garbage as command
// framing and be deaf to the first real command afterward. The next
// SDHC_Initialize()/SDHC_SetClockDivider() re-enables the clock.
void SDHC_StopClock(void);

// Sets SDHCCON1.DTXWIDTH for 1-bit or 4-bit transfer width. Caller
// (sd_card.c) must have already told the card to switch via ACMD6 first --
// this only changes the host side.
bool SDHC_SetBusWidth(bool wide4bit);

// Must be called before SDHC_SendCommand() for any command that moves
// data (CMD17/18/24/25 in sd_card.c's usage): programs SDHCBLKCON
// (BSIZE/BCOUNT) and records direction/DMA-use for SDHC_SendCommand() to
// fold into the Transfer Mode bits of the same SDHCMODE write that issues
// the command. Also sets SDHCCON1.DMASEL to select ADMA2 (0b10) whenever
// useADMA2 is honored, since DMASEL's power-on default (0b00) selects
// SDMA -- a mode this driver never programs the system-address register
// for, so leaving DMASEL untouched would give a live DMAEN=1 with no
// working destination. Has no effect on commands issued with
// dataPresent=false.
void SDHC_ConfigureBlockTransfer(uint16_t blockSize, uint16_t blockCount, bool isWrite, bool useADMA2);

// Waits CINHCMD (and CINHDAT, if dataPresent) clear, loads SDHCARG, then
// performs a single write to SDHCMODE that both configures the command
// (response type, CRC/index checking, data-present, command index) and
// atomically issues it -- on this PIC32 implementation SDHCMODE is one
// combined 32-bit Transfer-Mode-and-Command register, unlike the split
// 16+16 register pair in the generic SDHCI spec. Waits for CCIF (command
// complete) with a bounded timeout. Returns false on CTOEIF/CCRCEIF/
// CEBEIF/CIDXEIF or on timeout; clears whichever error flag(s) fired.
bool SDHC_SendCommand(uint8_t cmdIndex, uint32_t argument, sdhc_response_type_t responseType, bool dataPresent);

// Copies SDHCRESP0-3 (raw response) after a completed command. For R1/
// R1B/R3/R6R7, the 32-bit response is in response[0]; for R2 (CID/CSD),
// all four words are populated (this device's SDHCRESP0-3 hold the
// 128-bit payload with the same shift convention CID/CSD parsing expects
// -- see sd_card.c).
void SDHC_GetResponse(uint32_t response[4]);

// Polls BWEN/BREN and shifts SDHCDATA a 32-bit word at a time for
// `blockCount` blocks of `blockSize` bytes each (must match what was
// passed to the preceding SDHC_ConfigureBlockTransfer()/SDHC_SendCommand()
// pair). Then waits TXCIF (transfer complete) with a bounded timeout.
// This is the always-available fallback path -- first bring-up target,
// used automatically by sd_card.c whenever SDHC_IsADMA2Supported() is
// false or ADMA2 hasn't been validated yet.
bool SDHC_TransferBlocksPIO(uint8_t *buffer, uint16_t blockSize, uint16_t blockCount, bool isWrite);

// Builds a 32-bit ADMA2 descriptor table (single-descriptor, since
// `buffer` is assumed caller-contiguous) in a statically-allocated,
// uncached (KSEG1) region and points SDHCAADDR at its physical address.
// MUST be called BEFORE SDHC_SendCommand(..., dataPresent=true) for the
// same transfer -- the ADMA2 engine starts fetching descriptors from
// SDHCAADDR the moment the command goes out (SDHCMODE.DMAEN=1), so
// programming SDHCAADDR any later races the engine against an
// unprogrammed address register. Caller must first call
// SDHC_ConfigureBlockTransfer(..., useADMA2=true), which also sets
// SDHCCON1.DMASEL to select ADMA2 (not the power-on-default SDMA, which
// this driver does not support). On a write, also flushes the buffer out
// of the CPU's D-cache to physical RAM first (see SDHC_WaitADMA2Transfer()
// for the read-side counterpart) -- the ADMA2 engine only ever sees
// physical memory, not the cache. Returns false (falls through to
// SDHC_TransferBlocksPIO() being the caller's fallback) if the transfer
// exceeds a single descriptor's 65536-byte limit; multi-descriptor
// chaining is not implemented.
bool SDHC_PrepareADMA2Transfer(const uint8_t *buffer, uint16_t blockSize, uint16_t blockCount, bool isWrite);

// Waits TXCIF (transfer complete) with a bounded timeout, checking
// SDHCAESTAT-reflected errors (ADEIF) on failure, for an ADMA2 transfer
// previously armed by SDHC_PrepareADMA2Transfer() and issued via
// SDHC_SendCommand(). On a successful read, invalidates the buffer's
// D-cache lines afterward so the caller's next load re-fetches the data
// the ADMA2 engine just wrote to physical RAM instead of returning
// whatever was cached there beforehand. Gated by SDHC_IsADMA2Supported()
// -- sd_card.c falls back to SDHC_TransferBlocksPIO() transparently if
// this silicon instance doesn't support ADMA2.
bool SDHC_WaitADMA2Transfer(uint8_t *buffer, uint16_t blockSize, uint16_t blockCount, bool isWrite);

// Returns whether this silicon instance's SDHCCAP.ADMA2 bit is set.
// Cached at SDHC_Initialize() time.
bool SDHC_IsADMA2Supported(void);

// Returns true if the SDHC peripheral reports an active data-line
// transfer in progress (SDHCSTAT1 DLACTIVE/WRACTIVE/RDACTIVE). Used by
// sd_card.c to avoid dropping SD_PWR_EN_PIN mid-transfer.
bool SDHC_IsDataLineBusy(void);

// Returns the base clock (Hz) feeding SDCLKDIV, decoded from
// SDHCCAP.BASECLK (an 8-bit field in MHz, standard SDHCI convention) at
// SDHC_Initialize() time. This is the empirically-measured value
// SDHC_SetClockDivider() actually uses -- see the file header comment on
// why this driver reads it back instead of assuming a source.
uint32_t SDHC_GetBaseClockHz(void);

// SDHC interrupt service routine. Declared here, defined in sdhc.c.
// Minimal body: latches the interrupt-status bits a blocking wait loop
// above might be polling for into a volatile snapshot, then
// clearInterruptFlag(sdhc_interrupt) -- no SD-protocol logic runs here.
void __ISR(_SDHC_VECTOR, IPL2SRS) sdhcISR(void);

// Prints SDHC peripheral/controller settings only (PMD gating state,
// ICLK/SDCLK enabled + divider + computed Hz, DTXWIDTH, HSEN, ADMA2
// capability + DMAEN/DMASEL state, SDHCCAP raw dump, interrupt-enable
// summary, CARDINS/CDSLVL, and the SD_CARD_DETECT_PIN/SD_PWR_EN_PIN GPIO
// levels). Backs the "Peripheral Status? SDHC" USB UART command. Does NOT
// print CID/CSD/capacity/filesystem info -- that's SD_Card_PrintInfo()
// (sdhc/device_driver/sd_card.h), a separate command.
void SDHC_PrintStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* SDHC_H */
