/*******************************************************************************
  FLIR Lepton VoSPI (Video over SPI) Capture Driver

  File Name:
    flir_vospi.h

  Summary:
    Interrupt/DMA-driven capture of the FLIR Lepton 3.5's thermal video stream
    over SPI4, assembling complete 160x120 RAW14 frames in DDR2. Separate from
    the CCI control plane (flir_cci.c).

  Description:
    The Lepton is the SPI *slave*; the PIC32 SPI4 master generates the clock by
    continuously shifting dummy bytes out (SDO4/RD5, unused by the sensor) while
    capturing the sensor's data on SDI4/RD7. Chip select (nFLIR_VOSPI_CS, RD1)
    is a plain GPIO held low for the whole capture, matching the SPI3/flash CS
    convention. SCK4 is the dedicated RD10 pin.

    VoSPI framing (RAW14, Lepton 3.x "segmented" mode):
      - A packet is 164 bytes: a 4-byte header (2-byte ID + 2-byte CRC) plus a
        160-byte payload = 80 pixels x 2 bytes (14-bit value, big-endian).
      - "Discard" packets (ID high nibble == 0xF) carry no data and are skipped.
      - 60 packets make a segment; the segment number (1..4) is only valid in
        packet #20's ID field (bits 14:12). 4 segments make one 160x120 frame.
      - Each segment covers 30 image rows; two packets make one 160-pixel row
        (packet k -> row (seg-1)*30 + k/2, column half (k%2)*80).

    Capture path: two DMA channels move one packet at a time -- DCH3 shifts 164
    dummy bytes out to generate the clock, DCH2 captures the 164 received bytes
    into a packet buffer, and the DCH2 block-complete ISR parses the header,
    accumulates the segment, and re-arms both channels for the next packet. On a
    completed frame it byte-swaps/masks the payload into one of two DDR2 frame
    buffers and flips which one is "ready" for flir_process.c to read. VSYNC
    (Lepton GPIO3 -> RD0 -> INT1) provides frame-timing/statistics.

    This is a first-cut real-time path: the per-packet re-arm latency and the
    optional CRC check are documented tuning points, not yet hardware-proven.
*******************************************************************************/

#ifndef FLIR_VOSPI_H
#define FLIR_VOSPI_H

#include <stdint.h>
#include <stdbool.h>

#include "core/ddr2.h"

#ifdef __cplusplus
extern "C" {
#endif

// Lepton 3.5 native thermal resolution.
#define FLIR_VOSPI_WIDTH_PX             160u
#define FLIR_VOSPI_HEIGHT_PX            120u
#define FLIR_VOSPI_PIXELS               (FLIR_VOSPI_WIDTH_PX * FLIR_VOSPI_HEIGHT_PX)

// Packet / segment geometry (RAW14).
#define FLIR_VOSPI_PACKET_HEADER_BYTES  4u
#define FLIR_VOSPI_PACKET_PAYLOAD_BYTES 160u
#define FLIR_VOSPI_PACKET_BYTES         (FLIR_VOSPI_PACKET_HEADER_BYTES + FLIR_VOSPI_PACKET_PAYLOAD_BYTES)
#define FLIR_VOSPI_PACKETS_PER_SEGMENT  60u
#define FLIR_VOSPI_SEGMENTS_PER_FRAME   4u

// Two 160x120 14-bit frame buffers in DDR2 (uncached KSEG1 -- only the CPU
// touches them, capture ISR writes one while flir_process reads the other).
// Placed at DDR2 +8MB, clear of the reserved 0..7MB region and the Layer 2
// image buffer at +7MB; the full partition map lives in gui/gui.h.
#define FLIR_VOSPI_DDR2_BASE_ADDRESS    (DDR2_KSEG1_BASE_ADDRESS + 0x00800000u)
#define FLIR_VOSPI_FRAME_A_ADDRESS      (FLIR_VOSPI_DDR2_BASE_ADDRESS + 0x00000000u)
#define FLIR_VOSPI_FRAME_B_ADDRESS      (FLIR_VOSPI_DDR2_BASE_ADDRESS + 0x00010000u)
#define FLIR_VOSPI_FRAME_SIZE_BYTES     ((uint32_t)FLIR_VOSPI_PIXELS * 2u)

typedef struct
{
    uint32_t framesCaptured;   // completed 4-segment frames handed to the app
    uint32_t desyncCount;      // packet-number breaks (lost byte alignment)
    uint32_t segmentErrors;    // invalid segment id reported in packet 20
    uint32_t vsyncCount;       // rising edges seen on VSYNC/INT1
} FLIR_VOSPI_STATS;

// One-time SPI4 + DMA + INT1 register setup. Leaves SPI4 on but idle (no DMA
// armed, INT1 disabled, CS deasserted) -- capture does not begin until Start().
// Assumes PMDInitialize() left PMD5.SPI4MD clear (see application/power_saving.c).
void FLIR_VOSPI_Initialize(void);

// Begins continuous capture: flushes SPI4, asserts CS, arms both DMA channels,
// and enables the DMA + VSYNC interrupts. Idempotent.
void FLIR_VOSPI_Start(void);

// Stops capture: disables interrupts/DMA, deasserts CS. SPI4 stays on (module
// idle) so Start() can resume without reconfiguring.
void FLIR_VOSPI_Stop(void);

// True if a new complete frame has arrived since the last TakeFrame().
bool FLIR_VOSPI_FrameReady(void);

// Returns a pointer to the most recently completed frame (FLIR_VOSPI_PIXELS
// 14-bit values, row-major, already byte-swapped and masked) and clears the
// frame-ready flag. Returns NULL if no frame has completed yet. The buffer
// stays valid until the frame after next completes (double buffered).
const uint16_t *FLIR_VOSPI_TakeFrame(void);

// Copies the current capture statistics.
void FLIR_VOSPI_GetStats(FLIR_VOSPI_STATS *stats);

// Prints SPI4/DMA/INT1 configuration and capture statistics to the terminal.
void FLIR_VOSPI_PrintStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* FLIR_VOSPI_H */
