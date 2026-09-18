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

// Packets captured per DMA block. The first version of this driver used 1 --
// one DMA block per packet, re-armed in software from the block-complete ISR
// -- and that turned out to be the thing that broke capture: every re-arm is
// a chance for the RX channel and the byte stream to get out of step, and one
// lost byte deadlocks the block forever (nothing else clocks the sensor, so
// the block can never finish). Reading 60 packets per block gives the DMA one
// long uninterrupted run per segment instead of 60 short ones, cuts the ISR
// rate from ~6,400/s to ~106/s, and keeps the 164-byte packet grid aligned
// across blocks by construction (the block is an exact multiple of it).
#define FLIR_VOSPI_PACKETS_PER_BLOCK    60u
#define FLIR_VOSPI_BLOCK_BYTES          (FLIR_VOSPI_PACKETS_PER_BLOCK * FLIR_VOSPI_PACKET_BYTES)

// Two 160x120 14-bit frame buffers in DDR2, through the CACHED (KSEG0) alias:
// only the CPU ever touches them (the capture ISR memcpy()s one full while
// flir_process reads the other; the VoSPI DMA lands in vospiBlock, not here),
// so there is no coherency to manage and no reason to pay uncached DDR2
// single-beat latency -- through KSEG1 the ISR's segment placement and the
// render's two full-frame read passes were each a few ms of stalled CPU per
// frame. Placed at DDR2 +8MB, clear of the reserved 0..7MB region and the
// Layer 2 image buffer at +7MB; the full partition map lives in gui/gui.h.
#define FLIR_VOSPI_DDR2_BASE_ADDRESS    (DDR2_KSEG0_BASE_ADDRESS + 0x00800000u)
#define FLIR_VOSPI_FRAME_A_ADDRESS      (FLIR_VOSPI_DDR2_BASE_ADDRESS + 0x00000000u)
#define FLIR_VOSPI_FRAME_B_ADDRESS      (FLIR_VOSPI_DDR2_BASE_ADDRESS + 0x00010000u)
#define FLIR_VOSPI_FRAME_SIZE_BYTES     ((uint32_t)FLIR_VOSPI_PIXELS * 2u)

typedef struct
{
    uint32_t framesCaptured;   // completed 4-segment frames handed to the app
    uint32_t packetsCaptured;  // DMA blocks completed (every packet, any kind)
    uint32_t discardPackets;   // of those, ones the sensor marked "no data"
    uint32_t desyncCount;      // packet-number breaks (lost byte alignment)
    uint32_t segmentErrors;    // invalid segment id reported in packet 20
    uint32_t resyncCount;      // /CS idle windows taken to re-align the stream
    uint32_t rxOverflows;      // SPIROV events (each one kills the capture)
    uint32_t stallRecoveries;  // deadlocked partial blocks recovered (a lost
                               // RX byte leaves the block one short forever)
    uint32_t blankPackets;     // all-zero headers rejected (a dead MISO line
                               // being clocked in -- NOT a real packet 0)
    uint32_t blankBlocks;      // blocks in which every packet was blank; a
                               // short run of these forces a /CS resync

    // Chained mode only: stopped-clock recoveries done in place (~20ms, no
    // /CS window), plus the state the wedge was found in each time -- the
    // breakdown that identifies WHY the clock stops, which the /CS-window
    // recovery always threw away.
    uint32_t clockUnsticks;        // total in-place recoveries attempted
    uint32_t unstickRxDrains;      // RX FIFO held bytes w/ a live RX channel:
                                   // master paused on a full FIFO (no SPIROV
                                   // in master mode); drained via forced cells
    uint32_t unstickRxDead;        // RX FIFO held bytes with NO RX channel
                                   // enabled (chain enable lost) -> /CS window
    uint32_t unstickTxDisabled;    // TX found disabled (CHAEN re-enable lost)
    uint32_t unstickTxNeverStarted;// TX enabled but no byte of its block sourced
    uint32_t unstickTxMidBlock;    // TX stopped part-way through a block
    uint32_t segmentsPlaced;   // complete 60-packet segments written to a frame
    uint32_t segmentRestarts;  // early packet 0s (sensor truncating a segment
                               // it marked invalid) -- normal, not an error

    // How often each of the 8 possible values of the packet-20 segment field
    // has been seen. This settles what that field really carries: mostly 0
    // with some 1..4 is a healthy Lepton 3.x (0 = "segment not valid"); a
    // roughly even spread across all 8 means the field is not the segment
    // number at all and the frame geometry assumption is wrong.
    uint32_t segmentIdSeen[8];

    // How many times each segment was actually PLACED into a frame, indexed
    // 1..4. Placements concentrated on segment 1 with few continuations mean
    // the frame accumulation is being reset between segments; a roughly even
    // 1/2/3/4 spread means assembly is running and frames should follow.
    uint32_t segmentsPlacedById[FLIR_VOSPI_SEGMENTS_PER_FRAME + 1u];
    uint32_t maxPacketRun;     // longest unbroken run of in-sequence packets
                               // (60 = a whole segment; 4 segments = 1 frame)
    uint32_t vsyncCount;       // rising edges seen on VSYNC/INT1
} FLIR_VOSPI_STATS;

// Number of recent packets, and bytes of each, kept for "FLIR Packet Dump".
#define FLIR_VOSPI_DUMP_PACKETS         8u
#define FLIR_VOSPI_DUMP_BYTES           12u

// One-time SPI4 + DMA + INT1 register setup. Leaves SPI4 on but idle (no DMA
// armed, INT1 disabled, CS deasserted) -- capture does not begin until Start().
// Assumes PMDInitialize() left PMD5.SPI4MD clear (see application/power_saving.c).
void FLIR_VOSPI_Initialize(void);

// Begins capture. Enables the DMA + VSYNC interrupts and schedules the start;
// the first packet is actually clocked ~200ms later, by FLIR_VOSPI_Tasks(),
// because VoSPI packet alignment is set by where clocking begins and the
// sensor only restarts its stream on a packet boundary after /CS has been idle
// for ~185ms (see the resync commentary in flir_vospi.c). Idempotent.
void FLIR_VOSPI_Start(void);

// Stops capture: disables interrupts/DMA, deasserts CS. SPI4 stays on (module
// idle) so Start() can resume without reconfiguring.
void FLIR_VOSPI_Stop(void);

// Drives the parts of capture that must run in thread context: arming once the
// /CS idle window expires, and the stall watchdog that forces a re-alignment
// when packets stop being accepted. Call once per main-loop iteration while
// streaming (flir.c does this from FLIR_Tasks()).
void FLIR_VOSPI_Tasks(void);

// True if a new complete frame has arrived since the last TakeFrame().
bool FLIR_VOSPI_FrameReady(void);

// Returns a pointer to the most recently completed frame (FLIR_VOSPI_PIXELS
// 14-bit values, row-major, already byte-swapped and masked) and clears the
// frame-ready flag. Returns NULL if no frame has completed yet. The buffer
// stays valid until the frame after next completes (double buffered).
const uint16_t *FLIR_VOSPI_TakeFrame(void);

// Same buffer as FLIR_VOSPI_TakeFrame(), but WITHOUT clearing the frame-ready
// flag -- so the renderer's normal TakeFrame() flow is undisturbed. Returns
// the most recently completed frame whether or not it has already been
// consumed, and NULL only until the very first frame completes. This is what
// a still capture wants: the frame the user is looking at right now, which is
// by definition one flir.c has already rendered and therefore already taken.
//
// The buffer is live: capture keeps running and will overwrite it two frames
// from now, so a caller keeping the data must copy it out (and, in practice,
// stop the stream -- see application/still_capture.c).
const uint16_t *FLIR_VOSPI_PeekFrame(void);

// Copies the current capture statistics.
void FLIR_VOSPI_GetStats(FLIR_VOSPI_STATS *stats);

// Milliseconds since the DMA last delivered a packet of any kind (0 when not
// capturing). A steadily climbing value means the byte stream itself has
// stopped -- a different problem from never locking onto packet boundaries.
uint32_t FLIR_VOSPI_MsSinceLastPacket(void);

// One-word description of what capture is doing right now: "idle",
// "resyncing" (inside the /CS idle window), "hunting" (clocking, looking for
// the start of a segment), or "locked" (packet sequence tracking).
const char *FLIR_VOSPI_GetCaptureStateString(void);

// Prints SPI4/DMA/INT1 configuration and capture statistics to the terminal.
void FLIR_VOSPI_PrintStatus(void);

// Prints the leading bytes of the last few packets the DMA delivered, decoded
// (discard / packet number / segment). This is the one view that separates the
// ways capture can fail: no packets at all (DMA/interrupt problem), all 0xFF
// (nothing driving MISO), all 0x00 (MISO stuck low), genuine discard packets
// (sensor powered and framing fine but producing no video), or varied bytes
// that never form a packet sequence (byte misalignment / wrong SPI mode).
void FLIR_VOSPI_PrintPacketDump(void);

#ifdef __cplusplus
}
#endif

#endif /* FLIR_VOSPI_H */
