/*******************************************************************************
  SD Memory Card Protocol Driver

  File Name:
    sd_card.h

  Summary:
    SD memory card command/protocol layer built on top of sdhc.h. Knows
    what a CMD8 or ACMD41 is; sdhc.h does not.

  Description:
    This is the "device driver" layer in the sdhc.c (bus/peripheral) +
    sd_card.c (device) split, the same shape as spi3.c + sst25vf080b.c.
    It owns the card power/detect sequencing (SD_PWR_EN_PIN,
    SD_CARD_DETECT_PIN, gpio/pin_macros.h) and the full SD card
    identification state machine (CMD0 -> CMD8 -> ACMD41 -> CMD2 -> CMD3 ->
    CMD9 -> CMD7 -> ACMD6), and exposes block read/write plus CID/CSD-
    derived card metadata.

    SD_Card_ReadBlocks()/SD_Card_WriteBlocks() are the pair a future USB
    mass-storage class driver would call directly for raw block access,
    bypassing FatFs/diskio.c entirely -- see sdhc.h's file header for the
    layering rationale.

    Two things here are flagged as needing hardware-bring-up verification
    (both called out again at their point of use below): the polarity
    assumed for SD_CARD_DETECT_PIN (active-low, the standard mechanical-
    switch convention), and the SDHCRESP0-3 word-to-CID/CSD-bit mapping
    used to parse card metadata (a common but unconfirmed-for-this-silicon
    convention). Neither affects file I/O correctness if wrong -- CID/CSD
    are informational (the "SD Card Info?" command), and a flipped detect
    polarity just means the card looks permanently present/absent, which
    bring-up testing would surface immediately.
*******************************************************************************/

#ifndef SD_CARD_H
#define SD_CARD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SD_CARD_TYPE_UNKNOWN = 0,
    SD_CARD_TYPE_SDSC,   // Standard Capacity, byte-addressed, <= 2GB (4GB for some MMC-era cards)
    SD_CARD_TYPE_SDHC,   // High Capacity, block-addressed, up to 32GB
    SD_CARD_TYPE_SDXC    // eXtended Capacity, block-addressed, > 32GB
} sd_card_type_t;

typedef struct {
    sd_card_type_t type;
    uint32_t capacity_blocks;      // 512-byte blocks
    uint8_t  manufacturer_id;      // CID MID
    char     oem_id[3];            // CID OID (2 chars + NUL)
    char     product_name[6];      // CID PNM (5 chars + NUL)
    uint8_t  product_revision;     // CID PRV (upper nibble = major, lower = minor, BCD)
    uint32_t serial_number;        // CID PSN
    uint16_t manufacture_year;     // CID MDT decoded (calendar year)
    uint8_t  manufacture_month;    // CID MDT decoded (1-12)
    bool     high_speed_capable;   // reserved for the phase-2 High-Speed negotiation, always false for now
    bool     wide_bus_active;      // true once ACMD6 + SDHC_SetBusWidth(true) have both succeeded
} sd_card_info_t;

// Full power-up + card-detect + identification sequence:
//   1. SD_PWR_EN_PIN high, settling delay
//   2. SD_Card_IsPresent() debounced check -- returns false (power left
//      de-asserted) if no card responds; this is the normal "no card"
//      case, not treated as a fault by the caller (main.c)
//   3. CMD0 (GO_IDLE_STATE) -> CMD8 (SEND_IF_COND, probes v2.00+) ->
//      ACMD41 (SD_SEND_OP_COND, polled until busy clears, up to the SD
//      spec's 1 second allowance; HCS bit requests High Capacity support
//      if CMD8 succeeded) -> CMD2 (ALL_SEND_CID) -> CMD3
//      (SEND_RELATIVE_ADDR) -> CMD9 (SEND_CSD) -> CMD7 (SELECT_CARD) ->
//      CMD16 (SET_BLOCKLEN, SDSC only) -> ACMD6 (SET_BUS_WIDTH 4-bit) ->
//      SDHC_SetBusWidth(true) -> SDHC_SetClockDivider() up to Default
//      Speed (25MHz)
// Populates a static sd_card_info_t on success. Returns false (with
// SD_PWR_EN_PIN left de-asserted) if no card is detected or any
// identification step times out/errors.
bool SD_Card_Initialize(void);

// Debounced poll of SD_CARD_DETECT_PIN (gpio/pin_macros.h, RA0): several
// consecutive consistent reads a few hundred microseconds apart, no
// interrupt (Port A change-notification is already owned by
// portAChangeNoticeISR() in application/pushbuttons.c for the cap-touch
// pins -- only one ISR can be registered per CN vector, so this driver
// polls instead of adding a conflicting second one). ASSUMES ACTIVE-LOW
// (reads low = card inserted); verify against the schematic during
// bring-up if this is backwards.
bool SD_Card_IsPresent(void);

// Returns NULL if no card has been successfully initialized (SD_Card_
// Initialize() hasn't run, failed, or SD_Card_PowerDown() has since run).
const sd_card_info_t *SD_Card_GetInfo(void);

// CMD17/CMD18 (single/multi block read), dispatching to
// SDHC_TransferBlocksADMA2() if SDHC_IsADMA2Supported(), else
// SDHC_TransferBlocksPIO(). Handles SDSC (byte address = startBlock*512)
// vs SDHC/SDXC (block address = startBlock) addressing transparently, and
// issues CMD12 (STOP_TRANSMISSION) after any multi-block transfer since
// Auto CMD12 is not configured in sdhc.c. Returns false if no card is
// initialized or the transfer fails.
bool SD_Card_ReadBlocks(uint32_t startBlock, uint8_t *buffer, uint16_t blockCount);

// CMD24/CMD25 (single/multi block write). See SD_Card_ReadBlocks() for
// addressing/DMA/multi-block-stop behavior, which this mirrors.
bool SD_Card_WriteBlocks(uint32_t startBlock, const uint8_t *buffer, uint16_t blockCount);

// Waits (bounded) for SDHC_IsDataLineBusy() to clear -- no transfer left
// in flight -- then de-asserts SD_PWR_EN_PIN and clears the cached card
// info (SD_Card_GetInfo() returns NULL afterward). Called automatically
// on any SD_Card_Initialize() failure step; also the backing call for the
// "SD Eject" USB UART command so the card can be safely pulled.
bool SD_Card_PowerDown(void);

// Prints CID/CSD-derived card metadata to the terminal: manufacturer ID,
// OEM ID, product name/revision/serial number, manufacture date, card
// type (SDSC/SDHC/SDXC), capacity, and current bus width/speed mode.
// Backs the "SD Card Info?" USB UART command. Deliberately NOT part of
// SDHC_PrintStatus() (sdhc.h), which is peripheral-settings-only.
void SD_Card_PrintInfo(void);

#ifdef __cplusplus
}
#endif

#endif /* SD_CARD_H */
