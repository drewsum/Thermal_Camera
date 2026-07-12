/* ************************************************************************** */
/** DDR2 SDRAM Controller Driver

  @Company
    Marquette Senior Design E44

  @File Name
    ddr2.c

  @Summary
    Driver for the PIC32MZ2064DAR176's stacked-in-package 32MB DDR2 SDRAM

  @Description
    See ddr2.h for the memory map and integration notes. This file derives
    every DDR2 controller/PHY register value from:
      - This device's fixed DDR2 geometry and timing (PIC32MZ-DA family data
        sheet DS60001565, section 4.2 "DDR2 SDRAM" and Table 44-55 "DDR2
        SDRAM Timing Specifications"),
      - The register field formulas in the PIC32 Family Reference Manual,
        Section 55 "DDR SDRAM Controller" (DS60001321), Table 55-4, and
      - The standard JEDEC DDR2 (JESD79-2F) Mode/Extended Mode Register
        encodings (identical across every DDR2 vendor's datasheet).

    A handful of XC32 device-header bitfield definitions for this part
    disagree with the family reference manual's documented bit positions
    (confirmed for DDRMEMCFG4: the header widens BNKADDRMSK/CSADDRMSK to 5
    bits each where the manual specifies 3, shifting CSADDRMSK to the wrong
    bit). To avoid relying on other, unchecked bitfield names in the same
    register file, every register below that isn't a single full-width
    field is written as one masked/shifted 32-bit value with the bit
    position cited in the comment, rather than through the compiler's ".bits"
    struct.
 */
/* ************************************************************************** */

#include <xc.h>
#include <stdio.h>
#include <string.h>

#include "core/ddr2.h"
#include "core/device_control.h"
#include "usb_uart/terminal_control.h"
#include "application/error_handler.h"
#include "core/watchdog_timer.h"   // kickTheDog() -- serviced during the long self-test pass

// ---- SDRAM timing parameters, in PICOSECONDS.
//
// CURIOSITY-PARITY DIAGNOSTIC (2026-07-10): these are the values Microchip
// ships in the GENERATED, WORKING init for the same DAR silicon and internal
// die (gfx_apps_pic32mz_da, config glcd_..._mzda_intddr_cu = PIC32MZ DA
// Curiosity with internal DDR: plib_ddr.c with tCK = 2500ps, CL = 5), NOT the
// DS60001565 Table 44-55 numbers this driver previously used (those are kept
// in the trailing comments). The word-2 read collapse survived write-for-write
// register parity with the reference SOURCES; this build goes to full VALUE
// parity with the generated config for a board that demonstrably works.
#define DDR2_TRFC_PS            127500 // auto-refresh cycle time      (DS60001565: 75000)
#define DDR2_TWR_PS             15000  // write recovery time          (DS60001565: 15000)
#define DDR2_TWTR_PS            7500   // internal write-to-read delay (DS60001565: 10000)
#define DDR2_TRP_PS             12500  // precharge-to-active delay    (DS60001565: 20000)
#define DDR2_TRCD_PS            12500  // active-to-read/write delay   (DS60001565: 20000)
#define DDR2_TRRD_PS            7500   // row-to-row (RAS-to-RAS)      (DS60001565: 10000)
#define DDR2_TRTP_PS            7500   // internal read-to-precharge   (DS60001565: 10000)
#define DDR2_TRAS_PS            45000  // active-to-precharge minimum  (DS60001565: 40000)
#define DDR2_TRC_PS             57500  // row cycle time               (DS60001565: 65000)
#define DDR2_TFAW_PS            35000  // four-bank activation window  (DS60001565: 35000)
#define DDR2_TDLLK_DRAM_CLKS    200    // Standard JEDEC DDR2 DLL lock time (also this part's tXSRD, DDR24)
#define DDR2_TCKE_DRAM_CLKS     3      // DDR18: CKE minimum high/low pulse width
#define DDR2_TXP_DRAM_CLKS      2      // DDR25: exit precharge power-down delay
#define DDR2_TMRD_DRAM_CLKS     2      // DDR26: Mode Register Set command cycle delay
// Average refresh interval for -40C to 85C Tj (DDR17); halve this if the
// board can run hotter than 85C junction temperature
#define DDR2_TREFI_PS           7800000

// ---- Speed grade. AL (posted CAS additive latency) is 0, unused. ----
//
// CURIOSITY PARITY: CL = 5 -- the value in Microchip's generated, working
// config for this same silicon and die (paired with tCK = 2500ps below).
// (Earlier CL=5 test ran at 100MHz/BL8 with the pre-repair PHY config; this
// is the first test of the full coherent value set. WL, mode-register
// fields, RCASLAT/WCASLAT, and all delay formulas derive from this macro.)
#define DDR2_CAS_LATENCY        5
#define DDR2_READ_LATENCY       DDR2_CAS_LATENCY               // RL = AL + CL
#define DDR2_WRITE_LATENCY      (DDR2_READ_LATENCY - 1)         // WL = RL - 1 (DDR29)

// ---- MPLL / DDR2 clock, CURIOSITY PARITY. The working Curiosity internal-
// DDR config (plib_clk.c) programs CFGMPLL = 0x0B001901 from the same 24MHz
// EC POSC as this board: MPLLIDIV = 1, MPLLMULT = 25 (VCO = 600MHz, in the
// 400-1600MHz range), MPLLODIV1 = 3, MPLLODIV2 = 1 -> MPLL output = 200MHz.
// Its plib_ddr.c then computes ALL timing with tCK = 2500ps (400MHz) and
// CTL_CLK_PERIOD = 5000ps -- i.e. on the working board the DRAM clock is
// 2x the MPLL output (PHY doubles it; the earlier assumption here that
// "DRAM clock = MPLL output" was apparently a factor-2 misread of the clock
// tree). Match both halves exactly: MPLL 200MHz out + 2500ps timing base.
#define DDR2_MPLL_INPUT_DIVIDER     1
#define DDR2_MPLL_MULTIPLIER        25
#define DDR2_MPLL_OUTPUT_DIVIDER1   3
#define DDR2_MPLL_OUTPUT_DIVIDER2   1
#define DDR2_TCK_PS                 2500   // DRAM clock period used by all formulas

// The controller runs in half-rate mode (required on this device): its
// clock is half the DDR2 clock, so its period is twice the DRAM clock
// period. PIC32 FRM Table 55-4 calls this CTL_CLK_PER. (= 5000ps, matching
// the generated Curiosity config's CTRL_CLK_PERIOD exactly.)
#define DDR2_CTL_CLK_PERIOD_PS      (DDR2_TCK_PS * 2)
// Burst length term used by the FRM Table 55-4 delay formulas, in controller
// clocks (2 for the BL8 the DRAM runs; Microchip's references also use 2)
#define DDR2_BL                     2

#define CEIL_DIV(numerator, denominator)  (((numerator) + (denominator) - 1) / (denominator))
#define MAX2(a, b)  ((a) > (b) ? (a) : (b))

// Upper time bound for any single DDR2 bring-up hardware wait. Every one of
// these (MPLL regulator/lock, host command issue, self-calibration) completes
// in well under a millisecond on healthy hardware; 100ms is a generous ceiling
// that still guarantees boot proceeds if a stage never completes.
#define DDR2_SPIN_TIMEOUT_US    100000u

// Busy-wait until `condition` is true, giving up after DDR2_SPIN_TIMEOUT_US
// using the CP0 core timer (same time base as ddr2DelayNanoseconds). Sets the
// bool `success_result` to true if the condition was met, false on timeout, so
// callers can flag the failing stage and let main() keep booting instead of
// hanging forever.
#define DDR2_WAIT_UNTIL(condition, success_result)                             \
    do {                                                                       \
        uint32_t _ns_per_tick = 2000000000u / SYSCLK_INT;                      \
        uint32_t _timeout_ticks =                                              \
            ((uint32_t)DDR2_SPIN_TIMEOUT_US * 1000u) / _ns_per_tick;           \
        uint32_t _wait_start = _CP0_GET_COUNT();                               \
        (success_result) = true;                                               \
        while (!(condition)) {                                                 \
            if ((uint32_t)(_CP0_GET_COUNT() - _wait_start) >= _timeout_ticks) {\
                (success_result) = false;                                      \
                break;                                                         \
            }                                                                  \
        }                                                                      \
    } while (0)

// ---- Delay register fields, PIC32 FRM Table 55-4 formulas, computed in
// picoseconds. Where a formula disagrees with Microchip's generated Curiosity
// config, the generated config's version is used (noted inline) ----
#define DDR2_REFDLY         (CEIL_DIV(DDR2_TRFC_PS, DDR2_CTL_CLK_PERIOD_PS) - 2)   // -2 per generated config (was -1)
#define DDR2_W2PCHRGDLY     (CEIL_DIV(DDR2_TWR_PS, DDR2_CTL_CLK_PERIOD_PS) + DDR2_WRITE_LATENCY + DDR2_BL)
#define DDR2_PCHRGALLDLY    (CEIL_DIV(DDR2_TRP_PS, DDR2_CTL_CLK_PERIOD_PS))
#define DDR2_PCHRG2RASDLY   (CEIL_DIV(DDR2_TRP_PS, DDR2_CTL_CLK_PERIOD_PS) - 1)
#define DDR2_RAS2CASDLY     (CEIL_DIV(DDR2_TRCD_PS, DDR2_CTL_CLK_PERIOD_PS) - 1)
#define DDR2_RAS2RASDLY     (CEIL_DIV(DDR2_TRRD_PS, DDR2_CTL_CLK_PERIOD_PS) - 1)
#define DDR2_W2RDLY         (CEIL_DIV(DDR2_TWTR_PS, DDR2_CTL_CLK_PERIOD_PS) + DDR2_WRITE_LATENCY + DDR2_BL)
#define DDR2_W2RCSDLY       MAX2(DDR2_W2RDLY - 1, 3)
#define DDR2_R2PCHRGDLY     (CEIL_DIV(DDR2_TRTP_PS, DDR2_CTL_CLK_PERIOD_PS) + DDR2_BL - 2)
#define DDR2_SLFREFEXDLY    ((DDR2_TDLLK_DRAM_CLKS / 2) - 2)
#define DDR2_RAS2PCHRGDLY   (CEIL_DIV(DDR2_TRAS_PS, DDR2_CTL_CLK_PERIOD_PS) - 1)
#define DDR2_RAS2RASSBNKDLY (CEIL_DIV(DDR2_TRC_PS, DDR2_CTL_CLK_PERIOD_PS) - 1)
#define DDR2_FAWTDLY        (CEIL_DIV(DDR2_TFAW_PS, DDR2_CTL_CLK_PERIOD_PS) - 1)
#define DDR2_RMWDLY         (DDR2_READ_LATENCY - DDR2_WRITE_LATENCY + 3)
#define DDR2_R2WDLY         (DDR2_BL + 2)
#define DDR2_W2WCSDLY       (DDR2_BL - 1)
#define DDR2_W2WDLY         (DDR2_BL - 1)
#define DDR2_R2RCSDLY       (DDR2_BL)
#define DDR2_R2RDLY         (DDR2_BL - 1)
#define DDR2_RBENDDLY       (DDR2_READ_LATENCY + 3)
#define DDR2_NXTDATRQDLY    (DDR2_WRITE_LATENCY - 2)
#define DDR2_NXTDATAVDLY    4    // generated-config constant (formula RL+4 gave 9; reference uses 4)
#define DDR2_RDATENDLY      2    // generated-config constant (formula RL-2 gave 3; reference uses 2)
#define DDR2_REFCNT         (CEIL_DIV(DDR2_TREFI_PS, DDR2_CTL_CLK_PERIOD_PS) - 2)

// tCKE/tXP: the generated Curiosity config uses these directly as
// CONTROLLER-clock counts (SLFREFMINDLY/PWRDNMINDLY = tCKE-1, PWRDNEXDLY =
// tCKE since tCKE > tXP), rather than converting through time -- match it
#define DDR2_PWRDNMINDLY    (DDR2_TCKE_DRAM_CLKS - 1)
#define DDR2_SLFREFMINDLY   (DDR2_TCKE_DRAM_CLKS - 1)
#define DDR2_PWRDNEXDLY     (DDR2_TCKE_DRAM_CLKS)

// Minimum wait between Load Mode commands (tMRD), with a small fixed floor
// since the raw formula (1 controller clock) leaves no margin and this
// only runs once at boot
#define DDR2_MRS_WAIT       MAX2(CEIL_DIV(DDR2_TMRD_DRAM_CLKS * DDR2_TCK_PS, DDR2_CTL_CLK_PERIOD_PS), 4)

// ---- JEDEC DDR2 Mode Register (MR) fields (JESD79-2F; identical across
// every DDR2 vendor's datasheet, e.g. Micron's 256Mb DDR2 "Mode Register
// (MR) Definition") ----
// Burst length: 8. The DRAM must be BL8 while SCL calibrates on this part:
// programming BL4 at boot makes SCL time out (hardware-confirmed three times
// during 2026-07-10 bring-up, including under the final clock/pad config --
// SCL runs with SCLCFG0.BURST8 set and needs all 8 beats). Microchip's
// reference inits nominally program BL4, but BL8 passes the full 32MB self
// test here and is what SCL requires, so BL8 is this driver's operating
// point.
#define DDR2_MR_BURST_LENGTH_8      (0x3u << 0)   // M0-M2 = 011
#define DDR2_MR_BURST_TYPE_SEQ      (0x0u << 3)   // M3 = 0 (sequential)
#define DDR2_MR_CAS_LATENCY         (((uint32_t)DDR2_CAS_LATENCY & 0x7u) << 4)  // M4-M6 (JEDEC code = CL)
#define DDR2_MR_DLL_RESET           (0x1u << 8)   // M8 = 1 (self-clearing)
// Write recovery, computed from tCK so it tracks the DRAM clock:
// WR = ceil(tWR / tCK); JEDEC M11:M9 code = WR - 1.
// At tCK=2500ps: WR = 6, code 101 -- matches the generated Curiosity config.
#define DDR2_WR_CYCLES              CEIL_DIV(DDR2_TWR_PS, DDR2_TCK_PS)
#define DDR2_MR_WRITE_RECOVERY      (((DDR2_WR_CYCLES - 1u) & 0x7u) << 9)  // M9-M11
#define DDR2_MR_BASE  (DDR2_MR_BURST_LENGTH_8 | DDR2_MR_BURST_TYPE_SEQ | \
                        DDR2_MR_CAS_LATENCY | DDR2_MR_WRITE_RECOVERY)
#define DDR2_MR_VALUE            (DDR2_MR_BASE)
#define DDR2_MR_VALUE_DLL_RESET  (DDR2_MR_BASE | DDR2_MR_DLL_RESET)

// ---- JEDEC DDR2 Extended Mode Register (EMR/EMR1) fields ----
#define DDR2_EMR_DLL_ENABLE   (0x0u << 0)   // E0 = 0
#define DDR2_EMR_DRIVE_FULL   (0x0u << 1)   // E1 = 0 (full strength)
#define DDR2_EMR_RTT_DISABLED (0x0u)        // E2 = 0, E6 = 0 -- ODT unused, see ddr2ControllerConfigure()
#define DDR2_EMR_AL_0         (0x0u << 3)   // E3-E5 = 0 (posted CAS additive latency unused)
#define DDR2_EMR_OCD_EXIT     (0x0u << 7)   // E7-E9 = 000
#define DDR2_EMR_OCD_DEFAULT  (0x7u << 7)   // E7-E9 = 111
#define DDR2_EMR_BASE  (DDR2_EMR_DLL_ENABLE | DDR2_EMR_DRIVE_FULL | DDR2_EMR_RTT_DISABLED | DDR2_EMR_AL_0)
#define DDR2_EMR_VALUE             (DDR2_EMR_BASE | DDR2_EMR_OCD_EXIT)
#define DDR2_EMR_VALUE_OCD_DEFAULT (DDR2_EMR_BASE | DDR2_EMR_OCD_DEFAULT)

// EMR2/EMR3: all bits reserved on this part except EMR2 bit A7 (high-
// temperature self-refresh rate, left at its commercial/0 default here)
#define DDR2_EMR2_VALUE   0x0000
#define DDR2_EMR3_VALUE   0x0000

// Bank address (BA2:BA0) selecting which register a Load Mode command targets
#define DDR2_BANK_MR     0x0
#define DDR2_BANK_EMR    0x1
#define DDR2_BANK_EMR2   0x2
#define DDR2_BANK_EMR3   0x3

// Address bit A10 = 1 selects "all banks" for a Precharge command
#define DDR2_PRECHARGE_ALL_ADDR   (1u << 10)

// One entry in the JEDEC initialization command table (PIC32 FRM 55.5.1,
// Table 55-6). "ras"/"cas"/"we" are the first-cycle command truth table
// values; the second/subsequent cycle is always CKE-high/all-deselected
// per Table 55-6, so it isn't tracked per command here.
typedef struct {
    uint8_t  ras, cas, we;
    uint8_t  cs_active;    // 1 if CS0 is driven low (selected) for this command
    uint8_t  bank;         // BNKADDRCMD<2:0>
    uint16_t address;      // MDALCMD<7:0> / MDADDRHCMD<7:0>, split when packed
    uint16_t wait_cycles;  // controller clocks to hold before the next command
} ddr2_init_command_t;

static const ddr2_init_command_t ddr2_init_sequence[] = {

    // NOTE: wait_cycles here are in DRAM clocks (tCK), matching both
    // Microchip references' host-command delay formula (delay / T_CK); the
    // WAIT field was previously loaded with controller-clock counts, i.e.
    // half the reference durations.

    // a) Bring CKE high, hold for the mandatory >=400ns post-reset delay
    { 1, 1, 1, 0, 0, 0,
      CEIL_DIV(400000, DDR2_TCK_PS) },

    // b) Precharge all banks (tRP + margin)
    { 0, 1, 0, 1, 0, DDR2_PRECHARGE_ALL_ADDR,
      CEIL_DIV(DDR2_TRP_PS, DDR2_TCK_PS) + 2 },

    // c) EMR2 (commercial default)
    { 0, 0, 0, 1, DDR2_BANK_EMR2, DDR2_EMR2_VALUE,
      DDR2_MRS_WAIT },

    // d) EMR3 (all reserved)
    { 0, 0, 0, 1, DDR2_BANK_EMR3, DDR2_EMR3_VALUE,
      DDR2_MRS_WAIT },

    // e) EMR: enable DLL
    { 0, 0, 0, 1, DDR2_BANK_EMR, DDR2_EMR_VALUE,
      DDR2_MRS_WAIT },

    // f) MR: reset DLL. 200 DRAM clocks (tDLLK) must elapse before the DLL
    // may be trusted for a READ; held here rather than split across the
    // steps in between.
    { 0, 0, 0, 1, DDR2_BANK_MR, DDR2_MR_VALUE_DLL_RESET,
      DDR2_TDLLK_DRAM_CLKS },

    // g) Precharge all banks (tRP + margin)
    { 0, 1, 0, 1, 0, DDR2_PRECHARGE_ALL_ADDR,
      CEIL_DIV(DDR2_TRP_PS, DDR2_TCK_PS) + 2 },

    // h) Two auto-refresh commands, tRFC apart
    { 0, 0, 1, 1, 0, 0,
      CEIL_DIV(DDR2_TRFC_PS, DDR2_TCK_PS) },
    { 0, 0, 1, 1, 0, 0,
      CEIL_DIV(DDR2_TRFC_PS, DDR2_TCK_PS) },

    // i) MR again, DLL reset bit now cleared
    { 0, 0, 0, 1, DDR2_BANK_MR, DDR2_MR_VALUE,
      DDR2_MRS_WAIT },

    // j) EMR: OCD default state (all three OCD bits set)
    { 0, 0, 0, 1, DDR2_BANK_EMR, DDR2_EMR_VALUE_OCD_DEFAULT,
      DDR2_MRS_WAIT },

    // k) EMR: exit OCD calibration. Both references hold 140 tCK here, the
    // last command before normal operation/SCL, as remaining DLL-settle
    // margin.
    { 0, 0, 0, 1, DDR2_BANK_EMR, DDR2_EMR_VALUE,
      140 },

};

#define DDR2_NUM_INIT_COMMANDS  (sizeof(ddr2_init_sequence) / sizeof(ddr2_init_sequence[0]))

static bool ddr2_ready = false;

// private function prototypes
static bool ddr2MPLLInitialize(void);
static void ddr2ControllerConfigure(void);
static void ddr2PHYConfigure(void);
static void ddr2LoadInitCommands(void);
static bool ddr2RunSelfCalibration(void);
static void ddr2DelayNanoseconds(uint32_t nanoseconds);

// This function starts the dedicated Memory PLL (MPLL) to clock the DDR2
// PHY at 200MHz, then runs the controller/PHY/SDRAM bring-up sequence.
// Blocks until the SDRAM is ready for normal reads/writes.
bool ddr2Initialize(void) {

    if (ddr2_ready) return true;

    // Each stage below flags the error handler and aborts (rather than
    // spinning forever) if its hardware wait times out, so a DDR2 bring-up
    // failure leaves ddr2_ready false and lets main() continue booting. The
    // specific flag identifies which stage failed -- inspect it with the
    // "Error Status?" / "Peripheral Status? DDR2" serial commands.
    bool wait_ok;

    if (!ddr2MPLLInitialize()) return false;

    // The DDR2 clock must be stable for >=200us before any initialization
    // command is issued (PIC32 FRM 55.5.1, step 1)
    ddr2DelayNanoseconds(200000);

    ddr2ControllerConfigure();
    ddr2PHYConfigure();
    ddr2LoadInitCommands();

    // Issue the loaded command table, then wait for hardware to clear
    // VALID once every command (and its individual inter-command wait) has
    // been transmitted (PIC32 FRM 55.5.1, steps 5-6)
    DDRMEMCONbits.STINIT = 1;
    DDR2_WAIT_UNTIL(DDRCMDISSUEbits.VALID == 0, wait_ok);
    if (!wait_ok) {

        error_handler.flags.DDR2_init_sequence_timeout = 1;
        return false;

    }

    // Enable the controller for normal operation (PIC32 FRM 55.5.1, step 7)
    DDRMEMCONbits.INITDN = 1;

    // Calibrate read-capture and write-alignment timing now that the SDRAM
    // can service the read/write bursts SCL uses internally to calibrate
    if (!ddr2RunSelfCalibration()) {

        error_handler.flags.DDR2_calibration_timeout = 1;
        return false;

    }

    ddr2_ready = true;

    return true;

}

// This function returns true once ddr2Initialize() has completed and the
// SDRAM is ready for normal reads/writes
bool ddr2IsReady(void) {

    return ddr2_ready && DDRMEMCONbits.INITDN;

}

// this function sets up the dedicated Memory PLL (MPLL) that clocks the
// DDR2 PHY, independent of SYSCLK/PBCLKx -- see the CFGMPLL bit
// descriptions in the PIC32MZ-DA family data sheet, Register 41-14
static bool ddr2MPLLInitialize(void) {

    bool wait_ok;

    deviceUnlock();

    // Divider/multiplier/output-divider must be set while the MPLL is
    // still disabled (its POR default state)
    CFGMPLLbits.MPLLIDIV  = DDR2_MPLL_INPUT_DIVIDER;
    CFGMPLLbits.MPLLMULT  = DDR2_MPLL_MULTIPLIER;
    CFGMPLLbits.MPLLODIV1 = DDR2_MPLL_OUTPUT_DIVIDER1;
    CFGMPLLbits.MPLLODIV2 = DDR2_MPLL_OUTPUT_DIVIDER2;

    // Disable the internal DDRVREF divider (INTVREFCON = 0b00). Silicon
    // erratum #21 (DS80000736, module DDR2C) states the internal DDRVREF
    // circuit is NON-FUNCTIONAL on this part: it must be left off and an
    // external resistor divider on the DDRVREF pin must supply VDDR1V8/2.
    // Enabling it (0b11) leaves the DDR2 PHY without a valid read reference,
    // so self-calibration (ddr2RunSelfCalibration) never passes and
    // ddr2Initialize() hangs.
    // NOTE: this requires an external VDDR1V8/2 divider on DDRVREF -- confirm
    // it is populated on the board, otherwise DDR2 will still not calibrate.
    CFGMPLLbits.INTVREFCON = 0b00;

    // Enable the MPLL voltage regulator, then the MPLL itself -- the
    // regulator must be ready before MPLLDIS is cleared
    CFGMPLLbits.MPLLVREGDIS = 0;
    DDR2_WAIT_UNTIL(CFGMPLLbits.MPLLVREGRDY != 0, wait_ok);
    if (!wait_ok) {

        deviceLock();
        error_handler.flags.DDR2_mpll_vreg_timeout = 1;
        return false;

    }

    CFGMPLLbits.MPLLDIS = 0;
    DDR2_WAIT_UNTIL(CFGMPLLbits.MPLLRDY != 0, wait_ok);
    if (!wait_ok) {

        deviceLock();
        error_handler.flags.DDR2_mpll_lock_timeout = 1;
        return false;

    }

    deviceLock();

    return true;

}

// this function configures DDR2 memory geometry, refresh, and AC timing
static void ddr2ControllerConfigure(void) {

    // ---- Address geometry: {CS, ROW, BA, COL}, matching this part's
    // 4,194,304 x 4 banks x 16-bit organization (13 row / 2 bank / 9 column
    // address bits, one Chip Select) -- PIC32 FRM Example 55-1 walks this
    // same geometry end to end ----
    DDRMEMCFG0bits.CLHADDR = 0;      // column address is not split
    DDRMEMCFG2 = 0x00000000;        // CLADDRHMSK = 0 (no high column field)
    DDRMEMCFG3 = 0x000001FF;        // CLADDRLMSK = 9 column bits

    DDRMEMCFG0bits.BNKADDR = 9;      // bank field shifted right by the column width (9)

    DDRMEMCFG0bits.RWADDR = 11;      // row field shifted right by column+bank width (9+2)
    DDRMEMCFG1 = 0x00001FFF;        // RWADDRMSK = 13 row bits

    DDRMEMCFG0bits.CSADDR = 24;      // CS field shifted right by column+bank+row width (9+2+13)

    // DDRMEMCFG4: BNKADDRMSK<2:0> at bits 2:0, CSADDRMSK<2:0> at bits 8:6
    // (PIC32 FRM Register 55-10) -- written directly rather than through
    // DDRMEMCFG4bits, whose auto-generated field widths do not match the
    // manual for this register (see the file header comment)
    DDRMEMCFG4 = (0x3u << 0)   // BNKADDRMSK: 2 bank address bits
                | (0x0u << 6); // CSADDRMSK: one Chip Select, no address bits needed

    // Auto-precharge OFF, matching the generated Curiosity config
    // (APCHRGEN = 0; the controller manages rows itself). The earlier
    // AP-off attempt that "corrupted writes" ran under the old, wrong
    // clock/timing model. Bit 29 (U-Boot's "SB_PRI") is left clear -- the
    // generated config does not set it (tested live earlier: no effect).
    DDRMEMCFG0bits.APCHRGEN = 0;

    // ---- Refresh ----
    DDRREFCFGbits.REFCNT  = DDR2_REFCNT;
    DDRREFCFGbits.REFDLY  = DDR2_REFDLY;
    DDRREFCFGbits.MAXREFS = 0x7;    // allow refreshes to queue rather than stall accesses

    // Power config per the generated Curiosity values: auto power-down and
    // auto self-refresh stay disabled, but the self-refresh/power-down exit
    // delays are programmed (17 / 8) as the reference does
    DDRPWRCFG = 0x00000000;
    DDRPWRCFGbits.SLFREFDLY = 17;
    DDRPWRCFGbits.PWRDNDLY  = 8;

    // ---- AC timing (PIC32 FRM Table 55-4 formulas, using this part's
    // DS60001565 Table 44-55 timing parameters, computed above) ----
    DDRDLYCFG0bits.W2RDLY   = DDR2_W2RDLY & 0xF;
    DDRDLYCFG0bits.W2RCSDLY = DDR2_W2RCSDLY & 0xF;
    DDRDLYCFG0bits.R2RDLY   = DDR2_R2RDLY;
    DDRDLYCFG0bits.R2RCSDLY = DDR2_R2RCSDLY;
    DDRDLYCFG0bits.W2WDLY   = DDR2_W2WDLY;
    DDRDLYCFG0bits.W2WCSDLY = DDR2_W2WCSDLY;
    DDRDLYCFG0bits.R2WDLY   = DDR2_R2WDLY;
    DDRDLYCFG0bits.RMWDLY   = DDR2_RMWDLY;

    DDRDLYCFG1bits.SLFREFMINDLY = DDR2_SLFREFMINDLY;
    DDRDLYCFG1bits.SLFREFEXDLY  = DDR2_SLFREFEXDLY & 0xFF;
    DDRDLYCFG1bits.SLFREFEXDLY8 = (DDR2_SLFREFEXDLY >> 8) & 0x1;
    DDRDLYCFG1bits.PWRDNMINDLY  = DDR2_PWRDNMINDLY;
    DDRDLYCFG1bits.PWRDNEXDLY   = DDR2_PWRDNEXDLY;
    DDRDLYCFG1bits.W2PCHRGDLY4  = (DDR2_W2PCHRGDLY >> 4) & 0x1;
    DDRDLYCFG1bits.W2RDLY4      = (DDR2_W2RDLY >> 4) & 0x1;
    DDRDLYCFG1bits.W2RCSDLY4    = (DDR2_W2RCSDLY >> 4) & 0x1;
    DDRDLYCFG1bits.NXTDATAVDLY4 = (DDR2_NXTDATAVDLY >> 4) & 0x1;

    DDRDLYCFG2bits.PCHRGALLDLY  = DDR2_PCHRGALLDLY;
    DDRDLYCFG2bits.R2PCHRGDLY   = DDR2_R2PCHRGDLY;
    DDRDLYCFG2bits.W2PCHRGDLY   = DDR2_W2PCHRGDLY & 0xF;
    DDRDLYCFG2bits.RAS2RASDLY   = DDR2_RAS2RASDLY;
    DDRDLYCFG2bits.RAS2CASDLY   = DDR2_RAS2CASDLY;
    DDRDLYCFG2bits.PCHRG2RASDLY = DDR2_PCHRG2RASDLY;
    DDRDLYCFG2bits.RBENDDLY     = DDR2_RBENDDLY;

    DDRDLYCFG3bits.RAS2PCHRGDLY   = DDR2_RAS2PCHRGDLY;
    DDRDLYCFG3bits.RAS2RASSBNKDLY = DDR2_RAS2RASSBNKDLY;
    DDRDLYCFG3bits.FAWTDLY        = DDR2_FAWTDLY;

    // ---- On-die termination, per the generated Curiosity config: read ODT
    // off, WRITE ODT on with ODTWDLY = 1 / ODTWLEN = 3 (DDRODTENCFG bit 16
    // = ODTWEN; ODTWDLY = DDRODTCFG<15:12>, ODTWLEN = <22:20> per ATDF) ----
    DDRODTCFG = (1u << 12)      // ODTWDLY = 1
              | (3u << 20);     // ODTWLEN = 3
    DDRODTENCFG = (1u << 16);   // ODTWEN = 1, ODTREN = 0

    // ---- Transfer/burst configuration, generated-Curiosity values:
    // NXTDATRQDLY = 2 (= WL-2), NXTDATAVDLY = 4, RDATENDLY = 2, MAXBURST = 3.
    // Bits 30:28 stay 0 -- the generated config does not write them (they
    // were U-Boot-only, and were tested live earlier with no effect).
    // BIGENDIAN (bit 31) = 0: MIPS32 little-endian in this project.
    DDRXFERCFG = ((uint32_t)DDR2_NXTDATRQDLY << 0)          // NXTDATRQDLY<3:0>
               | ((uint32_t)(DDR2_NXTDATAVDLY & 0xF) << 4)  // NXTDATAVDLY<3:0> (bit 4 in DDRDLYCFG1<28>)
               | ((uint32_t)DDR2_RDATENDLY << 16)           // RDATENDLY<19:16>
               | (3u << 24);                                // MAXBURST<27:24>

    DDRMEMWIDTHbits.HALFRATE = 1;  // required -- this controller always runs in half-rate mode

}

// this function configures the DDR2 PHY: pad drive/ODT calibration, DLL,
// and PHY-side self-calibration (SCL) parameters. SCL itself is triggered
// separately, in ddr2RunSelfCalibration(), after the SDRAM is initialized.
static void ddr2PHYConfigure(void) {

    DDRPHYPADCONbits.HALFRATE   = 1;    // required, mirrors DDRMEMWIDTHbits.HALFRATE
    DDRPHYPADCONbits.NOEXTDLL   = 1;    // use the internal digital DLL; no external DLL populated
    DDRPHYPADCONbits.WRCMDDLY   = 1;    // required whenever WL (write latency) is even -- true for
                                        // both CL=3 (WL=2) and the CL=5 diagnostic (WL=4)
    DDRPHYPADCONbits.RCVREN     = 1;    // enable input receivers, needed for reads
    DDRPHYPADCONbits.PREAMBDLY  = 0b10; // preamble delay 2, matching BOTH Microchip
                                        // references (Harmony ddr_00184 default and
                                        // U-Boot PREAMBLE_DLY(2)); was 0 through
                                        // 2026-07-10 on a misread of this as a
                                        // write-preamble knob
    // PHY I/O-pad on-die termination, ENABLED at 150 ohm per PIC32 FRM
    // Table 55-8. It was briefly disabled on the assumption that this in-
    // package DDR2 (short die-to-die traces) would not need termination, and
    // self-calibration DOES still pass with ODT off -- but ddr2SelfTest() then
    // reports sporadic single-bit read errors, i.e. SCL tolerates the missing
    // termination while sustained data traffic does not. Keep ODT enabled: the
    // read data eye needs it for reliable operation, matching the FRM
    // recommendation. (Separate from the controller-scheduled DRAM Rtt in
    // DDRODTCFG/DDRODTENCFG, which stays off.)
    DDRPHYPADCONbits.ODTEN      = 1;    // ODT enabled
    DDRPHYPADCONbits.ODTSEL     = 1;    // 150 ohm termination
    // Pad calibration and drive strength: CURIOSITY PARITY -- the literal
    // values from Microchip's generated working config for this same die
    // (ODTPUCAL 3 / ODTPDCAL 2, drive select 0/0, FET drive strength 0xE).
    // The previous FRM-guessed values (PUCAL 2, DRVSEL 1/1, DRVSTR 0x8) were
    // fine at half data rate but are weaker drive than the reference; at the
    // corrected 400 MT/s these are load-bearing for SCL.
    DDRPHYPADCONbits.ODTPUCAL   = 0b11;
    DDRPHYPADCONbits.ODTPDCAL   = 0b10;
    DDRPHYPADCONbits.ADDCDRVSEL = 0;
    DDRPHYPADCONbits.DATDRVSEL  = 0;
    DDRPHYPADCONbits.DRVSTRNFET = 0xE;
    DDRPHYPADCONbits.DRVSTRPFET = 0xE;

    // Digital DLL (PIC32 FRM Table 55-9 recommended settings)
    DDRPHYDLLRbits.DLYSTVAL   = 0x3;
    DDRPHYDLLRbits.DISRECALIB = 0;      // keep periodic recalibration on -- useful across this
                                        // project's operating temperature range
    DDRPHYDLLRbits.RECALIBCNT = 0x10;

    // DDRADLLBYP (analog DLL bypass) and DDRPHYDLLCTRL (DDRDLLTRIM) are
    // deliberately NOT written. Through 2026-07-10 this driver bypassed the
    // analog DLL (DDRADLLBYP = 1<<24, on the assumption it "matched" NOEXTDLL)
    // and wrote DDRDLLTRIM = 0x01 -- but NEITHER Microchip reference init
    // (Harmony csp ddr_00184 plib_ddr, U-Boot drivers/ddr/microchip/ddr2.c)
    // touches either register: NOEXTDLL refers to an EXTERNAL DLL, and the
    // on-chip ANALOG DLL must stay active -- it generates the PHY's 90-degree
    // phase clocks for command launch and DQS gating. Bypassing it is the
    // leading suspect for the structural read fault (rigid word-2 burst
    // collapse, invariant to every digital delay/CL/clock-speed setting).

    // Self-Calibration Logic (SCL) parameters (PIC32 FRM Table 55-7
    // recommended settings). Written as raw values per the manual's bit
    // positions -- see the file header comment.
    DDRSCLLAT = (4u << 4)                        // DDRCLKDLY: recommended value
              | (3u << 0);                       // CAPCLKDLY: recommended value
    DDRSCLCFG0 = (1u << 24)                       // ODTCSW: enabled during SCL writes
               | ((uint32_t)DDR2_CAS_LATENCY << 4) // RCASLAT: DRAM read CAS latency
               | (1u << 1)                        // DDR: DDR2 is connected
               | (1u << 0);                       // BURST8: required in half-rate mode
    DDRSCLCFG1 = (0u << 12)                       // DBLREFDLY: single delay (recommended)
               | ((uint32_t)DDR2_WRITE_LATENCY << 8) // WCASLAT: DRAM write CAS latency
               | (1u << 0);                       // SCLCSEN: run SCL on Chip Select 0
    DDRSCLCFG2 = 0b00;                            // SCLLANSEL: calibrate both byte lanes

    // SCL test address: row 0 / column 0 / bank 0, always valid regardless of
    // geometry. (A column-4 diagnostic here on 2026-07-10 proved the pinned
    // read offset is NOT SCL residue -- the collapse did not move.)
    DDRPHYSCLADR = 0x00000000;

}

// this function loads the JEDEC DDR2 initialization command table
// (ddr2_init_sequence) into the DDRCMD1x/DDRCMD2x host command registers
static void ddr2LoadInitCommands(void) {

    // DDRCMD10..DDRCMD115 and DDRCMD20..DDRCMD215 are each 16 SFRs spaced 4
    // bytes apart in the device header -- addressed here as arrays via a
    // pointer to the first register in each block, rather than 16 separate
    // named assignments
    volatile uint32_t *ddrcmd1 = &DDRCMD10;
    volatile uint32_t *ddrcmd2 = &DDRCMD20;
    uint8_t i;

    for (i = 0; i < DDR2_NUM_INIT_COMMANDS; i++) {

        const ddr2_init_command_t *cmd = &ddr2_init_sequence[i];

        // Cycle 1 drives the actual command; cycle 2+ always reverts to
        // CKE high / all Chip Selects deselected (PIC32 FRM Table 55-6)
        uint32_t cs1 = cmd->cs_active ? 0xFEu : 0xFFu;

        // DDRCMD1x (PIC32 FRM Register 55-22)
        ddrcmd1[i] = (1u << 0)                         // CLKENCMD1
                   | (cs1 << 1)                         // CSCMD1<7:0>
                   | ((uint32_t)cmd->ras << 9)          // RASCMD1
                   | ((uint32_t)cmd->cas << 10)         // CASCMD1
                   | ((uint32_t)cmd->we  << 11)         // WENCMD1
                   | (1u << 12)                         // CLKENCMD2
                   | (0xFFu << 13)                      // CSCMD2<7:0> (deselected)
                   | (1u << 21) | (1u << 22) | (1u << 23) // RASCMD2/CASCMD2/WENCMD2 (deselected)
                   | (((uint32_t)cmd->address & 0xFFu) << 24); // MDALCMD<7:0>

        // DDRCMD2x (PIC32 FRM Register 55-23)
        ddrcmd2[i] = (((uint32_t)cmd->address >> 8) & 0xFFu)  // MDADDRHCMD<7:0>
                   | ((uint32_t)cmd->bank << 8)                // BNKADDRCMD<2:0>
                   | ((uint32_t)cmd->wait_cycles << 11);       // WAIT<8:0>

    }

    // NUMHOSTCMDS is a COUNT-MINUS-ONE field: both Microchip references load
    // 12 commands and write 0xB. (This driver wrote the raw count through
    // 2026-07-10, so every prior init transmitted one extra all-zeros command
    // -- CKE low, CS asserted -- after the JEDEC sequence.)
    DDRCMDISSUEbits.NUMHOSTCMDS = (uint8_t)(DDR2_NUM_INIT_COMMANDS - 1u);
    DDRCMDISSUEbits.VALID = 1;

}

// Number of times to (re)trigger self-calibration before giving up. SCL is
// marginal at the full 400 MT/s data rate and can leave one byte lane short
// of a pass on a given attempt; the reference (Harmony DDR_PHY_Calib)
// retries INDEFINITELY with the DDRPHYCLKDLY=0x10 nudge. Keep a bound so a
// truly dead part can't hang boot, but a generous one.
#define DDR2_SCL_MAX_ATTEMPTS   32

// this function runs PHY self-calibration as a TWO-STEP sequence, per
// Microchip's Harmony reference (csp peripheral/ddr_00184 plib_ddr.c,
// DDR_PHY_Calib), retrying up to DDR2_SCL_MAX_ATTEMPTS times:
//
//   1. SCLPHCAL|SCLSTART (bits 29+28): PHASE calibration -- aligns the
//      controller<->PHY clock-domain crossing. Completion is signaled by
//      SCLSTART (bit 28) SELF-CLEARING; the byte-lane pass bits do NOT
//      assert after this step.
//   2. SCLSTART alone: normal read-capture/write-alignment calibration.
//      After bit 28 self-clears, bits 1:0 (SCLUBPASS/SCLLBPASS) report the
//      per-lane result.
//   On any failure: write DDRPHYCLKDLY = 0x10 (Harmony's retry nudge) and
//   restart from the phase calibration.
//
// HISTORY / WRONG-CONCLUSION FIX (2026-07-10): early bring-up started phase
// cal but polled the PASS bits for completion, saw them never assert
// (DDRSCLSTART read 0x21100000 -- note bit 28 already CLEAR, i.e. phase cal
// had COMPLETED), concluded "SCLPHCAL hangs", and dropped it entirely. SCL
// then "passed" via step 2 alone, but with the controller<->PHY phase never
// calibrated -- the leading suspect for the structural read fault (reads
// issue column A[2:0]=100: a whole command-slot slip on the read path, while
// writes stay correct because WRCMDDLY puts them on a different slot).
// SCLEN (bit 26) stays clear as before: setting it wedges the engine.
static bool ddr2RunSelfCalibration(void) {

    bool wait_ok;
    uint8_t attempt;

    for (attempt = 0; attempt < DDR2_SCL_MAX_ATTEMPTS; attempt++) {

        // Step 1: phase calibration; complete when SCLSTART self-clears
        DDRSCLSTART = (1u << 29) | (1u << 28);
        DDR2_WAIT_UNTIL((DDRSCLSTART & (1u << 28)) == 0u, wait_ok);
        if (!wait_ok) {
            DDRPHYCLKDLY = 0x10;
            continue;
        }

        // Step 2: normal calibration; complete when SCLSTART self-clears
        DDRSCLSTART = (1u << 28);
        DDR2_WAIT_UNTIL((DDRSCLSTART & (1u << 28)) == 0u, wait_ok);
        if (!wait_ok) {
            DDRPHYCLKDLY = 0x10;
            continue;
        }

        // Bits 1:0 (SCLUBPASS/SCLLBPASS): upper/lower byte lane pass status
        if ((DDRSCLSTART & 0x3u) == 0x3u) {
            return true;
        }

        DDRPHYCLKDLY = 0x10;

    }

    return false;

}

// this function busy-waits for at least the given number of nanoseconds,
// using the CPU core timer (CP0 Count, which increments at SYSCLK/2). Used
// instead of this project's generic softwareDelay() because DDR2 bring-up
// has hard minimum wait requirements (JESD79-2F) that an uncalibrated NOP
// loop cannot guarantee.
static void ddr2DelayNanoseconds(uint32_t nanoseconds) {

    const uint32_t ns_per_tick = 2000000000u / SYSCLK_INT;
    uint32_t ticks = CEIL_DIV(nanoseconds, ns_per_tick);
    uint32_t start = _CP0_GET_COUNT();

    while ((uint32_t)(_CP0_GET_COUNT() - start) < ticks);

}

// This function copies "length" bytes from DDR2 SDRAM starting at byte
// "offset" into "destination". Requests past the end of the 32MB are
// silently truncated to fit.
void ddr2Read(uint32_t offset, void *destination, uint32_t length) {

    if (offset >= DDR2_SIZE_BYTES) return;
    if (length > DDR2_SIZE_BYTES - offset) length = DDR2_SIZE_BYTES - offset;

    memcpy(destination, (const void *)(DDR2_KSEG1_BASE_ADDRESS + offset), length);

}

// This function copies "length" bytes from "source" into DDR2 SDRAM
// starting at byte "offset". Requests past the end of the 32MB are
// silently truncated to fit.
void ddr2Write(uint32_t offset, const void *source, uint32_t length) {

    if (offset >= DDR2_SIZE_BYTES) return;
    if (length > DDR2_SIZE_BYTES - offset) length = DDR2_SIZE_BYTES - offset;

    memcpy((void *)(DDR2_KSEG1_BASE_ADDRESS + offset), source, length);

}

// This function exercises the DDR2 SDRAM to verify real data integrity (a
// passing self-calibration only proves the PHY captured one test address, not
// that the whole array stores and returns data). It runs three standard
// memory tests and prints a colored pass/fail for each, returning true only
// if all pass:
//   1. Data-bus walking-1s at one address  -> catches stuck/shorted DQ lines
//   2. Address-bus test (power-of-two aliasing) -> catches stuck/shorted address lines
//   3. Full-array cell test (pattern / inverted-pattern) -> catches stuck cells
//
// WARNING: this OVERWRITES the entire 32MB of DDR2. It is a bring-up/diagnostic
// tool -- do not run it once anything (framebuffer, heap, buffers) is using
// DDR2. Accesses go through the uncached (KSEG1) alias so the cache cannot
// hide a DRAM fault. The full-array pass takes a few seconds; the watchdog is
// kicked as it runs.
bool ddr2SelfTest(void) {

    volatile uint32_t *ddr = (volatile uint32_t *)DDR2_KSEG1_BASE_ADDRESS;
    const uint32_t num_words = DDR2_SIZE_BYTES / sizeof(uint32_t);
    const uint32_t seed = 0xA5A5A5A5u;   // XORed with the word index for a non-trivial per-cell value
    bool overall_pass = true;
    uint32_t i;

    terminalTextAttributesReset();
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("DDR2 SDRAM Self-Test:\r\n");

    if (!ddr2IsReady()) {
        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    DDR2 is not initialized/ready -- aborting.\r\n");
        terminalTextAttributesReset();
        return false;
    }

    terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    WARNING: this overwrites all 32MB of DDR2. Do not run if\r\n"
           "    anything (framebuffer, heap) is using it.\r\n");

    // ---- Probe: distinct-marker readback (characterizes the fault type) ----
    // Writes an unrelated, non-linear marker to each of the first N words, then
    // reads them back. Because the markers are unrelated to the address, the
    // failure mode is self-identifying:
    //   read == another word's marker  -> ADDRESS-line fault (aliasing)
    //   read == own marker w/ bits set -> DATA-line fault (diff mask shown)
    //   read == own marker             -> OK
    // (This disambiguates what the A5A5A5A5-seeded tests below cannot: with
    // that seed a stuck data bit 1 and a word0<->word2 address alias both read
    // back as 0xA5A5A5A7.)
    {
        static const uint32_t marker[16] = {
            0x1F2E3D4Cu, 0x5B6A7988u, 0xC3D2E1F0u, 0x0A1B2C3Du,
            0x9E8F7A6Bu, 0x4D5C6E7Fu, 0xF0E1D2C3u, 0x33445566u,
            0xAABBCCDDu, 0x778899AAu, 0x12FEDC34u, 0x56BA9876u,
            0xDEADC0DEu, 0xFEEDFACEu, 0x8BADF00Du, 0xB16B00B5u
        };
        uint32_t w, r;

        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    [probe] Distinct-marker readback (first 16 words):\r\n");

        for (w = 0; w < 16u; w++) ddr[w] = marker[w];   // write all first, so aliasing is visible

        for (w = 0; w < 16u; w++) {
            r = ddr[w];
            if (r == marker[w]) {
                terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
                printf("        word %2u @0x%06X: wrote 0x%08X read 0x%08X  OK\r\n",
                        (unsigned)w, (unsigned)(w * 4u), (unsigned)marker[w], (unsigned)r);
            } else {
                int alias_of = -1;
                uint32_t j;
                for (j = 0; j < 16u; j++) {
                    if (j != w && r == marker[j]) { alias_of = (int)j; break; }
                }
                terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
                if (alias_of >= 0) {
                    printf("        word %2u @0x%06X: wrote 0x%08X read 0x%08X  <- word %d's marker (ADDRESS aliasing)\r\n",
                            (unsigned)w, (unsigned)(w * 4u), (unsigned)marker[w], (unsigned)r, alias_of);
                } else {
                    printf("        word %2u @0x%06X: wrote 0x%08X read 0x%08X  diff=0x%08X (DATA bits)\r\n",
                            (unsigned)w, (unsigned)(w * 4u), (unsigned)marker[w], (unsigned)r,
                            (unsigned)(r ^ marker[w]));
                }
            }
        }

        // Read-vs-write discriminator: within one collapsing 16-byte block,
        // write a value to word 2 (offset 0x8, the slot that reads back), then
        // write a different value to word 0. If word 0's write leaves word 2
        // intact, the collapse is on the READ path (reads mis-address); if it
        // overwrites word 2's slot, the collapse is on the WRITE path.
        {
            uint32_t r2;
            ddr[2] = 0x11111111u;   // word 2 @0x08 -- the slot the block reads back
            ddr[0] = 0x22222222u;   // word 0 @0x00 -- should not touch word 2 if addressing is right
            r2 = ddr[2];
            terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("    [probe] read/write discriminator: wrote 11111111@w2 then 22222222@w0; w2 reads 0x%08X\r\n",
                    (unsigned)r2);
            if (r2 == 0x11111111u)
                printf("            -> w0 write did NOT disturb w2: burst collapse is on the READ path\r\n");
            else if (r2 == 0x22222222u)
                printf("            -> w0 write landed in w2's slot: burst collapse is on the WRITE path\r\n");
            else
                printf("            -> unexpected value: mixed/other fault\r\n");
        }

        // Cached vs uncached cross-check. Writes are known-good (discriminator),
        // so write 4 distinct words to a cold 16-byte block via the UNCACHED
        // alias, then read them back via BOTH aliases. A cached read is a
        // 16-byte cache-line burst fill (the whole burst at once); an uncached
        // read is a single 32-bit access. If the cached column comes back
        // correct while the uncached column collapses, the DRAM/burst is fine
        // and only the single-access uncached read path is broken -> DDR must
        // be accessed cached (KSEG0) with cache management, not via KSEG1.
        // (Relies on the cache line being cold: nothing has touched DDR through
        // the cached alias yet at this point in the test.)
        {
            const uint32_t blk = 0x00010000u;   // byte offset of a cold test block (64KB in)
            volatile uint32_t *unc = (volatile uint32_t *)(DDR2_KSEG1_BASE_ADDRESS + blk);
            volatile uint32_t *cac = (volatile uint32_t *)(DDR2_KSEG0_BASE_ADDRESS + blk);
            static const uint32_t tv[4] = { 0x0BADCAFEu, 0x1CEDC0DEu, 0x2FACEFEEu, 0x3D1CE5A5u };
            uint32_t k;

            for (k = 0; k < 4u; k++) unc[k] = tv[k];   // write via uncached (known-good path)

            terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("    [probe] cached vs uncached read of one 16-byte block:\r\n");
            for (k = 0; k < 4u; k++) {
                printf("            word %u: wrote 0x%08X  uncached 0x%08X  cached 0x%08X\r\n",
                        (unsigned)k, (unsigned)tv[k], (unsigned)unc[k], (unsigned)cac[k]);
            }
        }
    }

    // ---- Test 1: Data bus, walking-1s at a single address ----
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    [1/3] Data bus (walking-1s)........ ");
    {
        uint32_t stuck_bits = 0;
        uint8_t bit;
        for (bit = 0; bit < 32; bit++) {
            uint32_t pattern = (uint32_t)1u << bit;
            ddr[0] = pattern;
            stuck_bits |= (ddr[0] ^ pattern);   // any differing bit is a fault
        }
        if (stuck_bits == 0) {
            terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("PASS\r\n");
        } else {
            terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("FAIL (faulty data bits: 0x%08X)\r\n", (unsigned)stuck_bits);
            overall_pass = false;
        }
    }

    // ---- Test 2: Address bus, power-of-two aliasing (Barr algorithm) ----
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    [2/3] Address bus.................. ");
    {
        const uint32_t pattern = 0xAAAAAAAAu;
        const uint32_t antipattern = 0x55555555u;
        uint32_t offset, test;
        bool addr_pass = true;

        // Seed every power-of-two word address with the pattern.
        for (offset = 1u; offset < num_words; offset <<= 1) ddr[offset] = pattern;

        // Write the antipattern to the base; any power-of-two address that now
        // reads the antipattern is aliased to base (address bit stuck high).
        ddr[0] = antipattern;
        for (offset = 1u; offset < num_words; offset <<= 1) {
            if (ddr[offset] != pattern) { addr_pass = false; break; }
        }
        ddr[0] = pattern;

        // Walk the antipattern across each power-of-two address; base or any
        // other power-of-two address changing reveals a shorted/stuck address bit.
        for (test = 1u; addr_pass && test < num_words; test <<= 1) {
            ddr[test] = antipattern;
            if (ddr[0] != pattern) { addr_pass = false; break; }
            for (offset = 1u; offset < num_words; offset <<= 1) {
                if (offset != test && ddr[offset] != pattern) { addr_pass = false; break; }
            }
            ddr[test] = pattern;
        }

        if (addr_pass) {
            terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("PASS\r\n");
        } else {
            terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("FAIL (address line aliasing)\r\n");
            overall_pass = false;
        }
    }

    // ---- Test 3: Full-array cell test ----
    // Fill with an address-derived pattern, verify, rewrite inverted, verify --
    // proves every cell holds both a 0 and a 1. One dot printed per ~1MB.
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    [3/3] Full 32MB cell test (a few seconds):\r\n        ");
    {
        bool cell_pass = true;
        uint32_t fail_addr = 0, fail_exp = 0, fail_got = 0;

        // Pass A: write pattern
        for (i = 0; i < num_words; i++) {
            ddr[i] = i ^ seed;
            if ((i & 0x0000FFFFu) == 0u) kickTheDog();  // ~every 256KB (WDT ~250ms)
            if ((i & 0x0003FFFFu) == 0u) printf(".");   // ~every 1MB progress dot
        }
        // Pass B: verify pattern, then write inverted
        for (i = 0; cell_pass && i < num_words; i++) {
            uint32_t expect = i ^ seed;
            uint32_t got = ddr[i];
            if (got != expect) { cell_pass = false; fail_addr = i; fail_exp = expect; fail_got = got; break; }
            ddr[i] = ~expect;
            if ((i & 0x0000FFFFu) == 0u) kickTheDog();  // ~every 256KB (WDT ~250ms)
            if ((i & 0x0003FFFFu) == 0u) printf(".");   // ~every 1MB progress dot
        }
        // Pass C: verify inverted
        for (i = 0; cell_pass && i < num_words; i++) {
            uint32_t expect = ~(i ^ seed);
            uint32_t got = ddr[i];
            if (got != expect) { cell_pass = false; fail_addr = i; fail_exp = expect; fail_got = got; break; }
            if ((i & 0x0000FFFFu) == 0u) kickTheDog();  // ~every 256KB (WDT ~250ms)
            if ((i & 0x0003FFFFu) == 0u) printf(".");   // ~every 1MB progress dot
        }

        printf("\r\n");
        if (cell_pass) {
            terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("        PASS (%u words / 32MB verified)\r\n", (unsigned)num_words);
        } else {
            terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
            printf("        FAIL at word %u (byte offset 0x%08X): expected 0x%08X, got 0x%08X\r\n",
                    (unsigned)fail_addr, (unsigned)(fail_addr * 4u),
                    (unsigned)fail_exp, (unsigned)fail_got);
            overall_pass = false;
        }
    }

    // ---- Summary ----
    if (overall_pass) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    Overall: %s\r\n", overall_pass ? "PASS" : "FAIL");
    terminalTextAttributesReset();

    if (!overall_pass) error_handler.flags.DDR2_self_test_failed = 1;

    return overall_pass;

}

// This function prints the DDR2 controller/PHY configuration and status
void printDDR2Status(void) {

    terminalTextAttributesReset();
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("DDR2 SDRAM Controller Status:\r\n");

    // ---- Overall readiness ----
    if (ddr2IsReady()) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    Overall State: %s\r\n",
            ddr2IsReady() ? "Ready for reads/writes" : "NOT ready");

    // ---- Clocks and PLL ----
    // MFVCO = (POSC / MPLLIDIV) * MPLLMULT; DDR clock = MFVCO / (ODIV1 * ODIV2).
    // POSC is 24MHz on this board (external clock, POSCMOD=EC). These fields are
    // direct-encoded on this part (unlike the SYSPLL).
    const uint32_t posc_mhz = 24u;
    uint32_t mpll_idiv  = CFGMPLLbits.MPLLIDIV  ? CFGMPLLbits.MPLLIDIV  : 1;
    uint32_t mpll_mult  = CFGMPLLbits.MPLLMULT;
    uint32_t mpll_odiv1 = CFGMPLLbits.MPLLODIV1 ? CFGMPLLbits.MPLLODIV1 : 1;
    uint32_t mpll_odiv2 = CFGMPLLbits.MPLLODIV2 ? CFGMPLLbits.MPLLODIV2 : 1;
    uint32_t mpll_vco_mhz = (posc_mhz * mpll_mult) / mpll_idiv;
    uint32_t ddr_clk_mhz  = mpll_vco_mhz / (mpll_odiv1 * mpll_odiv2);

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    Clocks and PLL:\r\n");

    if (CFGMPLLbits.MPLLRDY) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("        Memory PLL (MPLL): %s\r\n", CFGMPLLbits.MPLLRDY ? "Locked" : "Not Ready");

    if (CFGMPLLbits.MPLLVREGRDY) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("        MPLL Voltage Regulator: %s\r\n", CFGMPLLbits.MPLLVREGRDY ? "Ready" : "Not Ready");

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("        MPLL VCO: %u MHz (%u MHz POSC / %u x %u)\r\n", mpll_vco_mhz, posc_mhz, mpll_idiv, mpll_mult);
    printf("        MPLL Output: %u MHz (VCO / %u / %u)\r\n", ddr_clk_mhz, mpll_odiv1, mpll_odiv2);
    // Clock model per the working Curiosity reference (MPLL 200MHz out with
    // 2500ps/400MHz timing): DRAM clock = 2 x MPLL output, controller (half
    // rate) = MPLL output
    printf("        DDR2 (DRAM) Clock: %u MHz (2 x MPLL output, per reference clock model)\r\n", ddr_clk_mhz * 2);
    printf("        Controller Clock: %u MHz (half-rate, DRAM clock / 2)\r\n", ddr_clk_mhz);
    printf("        Data Rate: %u MT/s (DDR, both clock edges)\r\n", ddr_clk_mhz * 4);

    // DDRVREF: the internal divider is non-functional on this silicon (erratum
    // #21). INTVREFCON must read 0 and an external divider must supply VDDR1V8/2.
    if (CFGMPLLbits.INTVREFCON == 0) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("        DDRVREF Source: %s (INTVREFCON=%u)\r\n",
            CFGMPLLbits.INTVREFCON == 0
                ? "External divider (internal disabled per erratum #21)"
                : "Internal (NON-FUNCTIONAL, see erratum #21)",
            (unsigned)CFGMPLLbits.INTVREFCON);

    // ---- Controller / operation ----
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    Controller:\r\n");

    if (DDRMEMCONbits.INITDN) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("        Initialization: %s\r\n",
            DDRMEMCONbits.INITDN ? "Done (normal operation)" : "Not complete");

    if ((DDRSCLSTART & 0x3) == 0x3) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("        Self-Calibration (SCL): lower lane %s, upper lane %s\r\n",
            (DDRSCLSTART & 0x1) ? "Passed" : "Failed/Not Run",
            (DDRSCLSTART & 0x2) ? "Passed" : "Failed/Not Run");

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("        Transfer Mode: %s\r\n", DDRMEMWIDTHbits.HALFRATE ? "Half-rate" : "Full-rate");
    printf("        Auto-Precharge: %s\r\n",
            DDRMEMCFG0bits.APCHRGEN ? "Enabled (every access)" : "Disabled");
    // ---- On-Die Termination (ODT) detail ----
    // ODT is a two-step enable per PIC32 FRM 55.4.3: the PHY pad must have ODT
    // enabled (DDRPHYPADCON.ODTEN) AND the controller must assert it per
    // access (DDRODTENCFG.ODTREN for reads, .ODTWEN for writes, with start/
    // duration timing in DDRODTCFG). Pad-enabled but not controller-asserted
    // means no termination actually happens during the SCL read bursts.
    {
        unsigned odt_ren = (unsigned)(DDRODTENCFG & 0x1u);          // ODTREN  (read ODT assert)
        unsigned odt_wen = (unsigned)((DDRODTENCFG >> 16) & 0x1u);  // ODTWEN  (write ODT assert)
        unsigned odt_csw = (unsigned)((DDRSCLCFG0 >> 24) & 0x1u);   // ODTCSW  (ODT during SCL writes)

        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
        printf("        On-Die Termination (ODT):\r\n");

        // PHY pad ODT enable + impedance
        if (DDRPHYPADCONbits.ODTEN) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("            PHY pad ODT (ODTEN): %s\r\n", DDRPHYPADCONbits.ODTEN ? "Enabled" : "Disabled");

        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("            Termination (ODTSEL): %s\r\n", DDRPHYPADCONbits.ODTSEL ? "150 ohm" : "75 ohm");
        printf("            Pad cal (ODTPUCAL/ODTPDCAL): %u / %u\r\n",
                (unsigned)DDRPHYPADCONbits.ODTPUCAL, (unsigned)DDRPHYPADCONbits.ODTPDCAL);

        // Controller assertion -- the part that actually terminates a transfer.
        // Read ODT off is highlighted because SCL calibrates on read bursts.
        if (odt_ren) terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        else terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("            Controller read ODT (ODTREN): %s\r\n", odt_ren ? "Asserted" : "NOT asserted");

        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("            Controller write ODT (ODTWEN): %s\r\n", odt_wen ? "Asserted" : "Not asserted");
        printf("            ODT during SCL writes (ODTCSW): %s\r\n", odt_csw ? "Enabled" : "Disabled");
        printf("            DDRODTCFG=0x%08X  DDRODTENCFG=0x%08X\r\n",
                (unsigned)DDRODTCFG, (unsigned)DDRODTENCFG);
    }

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("        Data Endianness: %s\r\n",
            DDRXFERCFGbits.BIGENDIAN ? "Big-endian" : "Little-endian");

    // ---- Timing ----
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    Timing:\r\n");
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("        CAS Latency (CL): %d, Read Latency (RL): %d, Write Latency (WL): %d\r\n",
            DDR2_CAS_LATENCY, DDR2_READ_LATENCY, DDR2_WRITE_LATENCY);
    printf("        Burst Length: %u (DRAM mode register)\r\n",
            ((DDR2_MR_BASE & 0x7u) == 0x3u) ? 8u : 4u);
    printf("        DRAM Period (tCK): %d ps, Controller Period: %d ps\r\n",
            DDR2_TCK_PS, DDR2_CTL_CLK_PERIOD_PS);
    printf("        AC timing (ps): tRCD %d, tRP %d, tRAS %d, tRC %d, tRFC %d, tWR %d\r\n",
            DDR2_TRCD_PS, DDR2_TRP_PS, DDR2_TRAS_PS, DDR2_TRC_PS, DDR2_TRFC_PS, DDR2_TWR_PS);
    printf("        Refresh Interval (tREFI): %d ps  [REFCNT=%u, REFDLY=%u, MAXREFS=%u]\r\n",
            DDR2_TREFI_PS, (unsigned)DDRREFCFGbits.REFCNT,
            (unsigned)DDRREFCFGbits.REFDLY, (unsigned)DDRREFCFGbits.MAXREFS);

    // ---- Geometry / capacity / address map ----
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    Geometry and Address Map:\r\n");
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("        Organization: 13 row / 2 bank / 9 column bits, 16-bit data, 1 chip select\r\n");
    printf("        Capacity: 32 MB (4,194,304 locations x 4 banks x 16 bits)\r\n");
    printf("        Physical: 0x%08X   KSEG0 (cached): 0x%08X   KSEG1 (uncached): 0x%08X\r\n",
            DDR2_PHYSICAL_BASE_ADDRESS, DDR2_KSEG0_BASE_ADDRESS, DDR2_KSEG1_BASE_ADDRESS);

    // ---- Raw register snapshot (for bring-up debugging) ----
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    Raw Registers:\r\n");
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("        CFGMPLL=0x%08X  DDRMEMCON=0x%08X  DDRSCLSTART=0x%08X\r\n",
            (unsigned)CFGMPLL, (unsigned)DDRMEMCON, (unsigned)DDRSCLSTART);
    printf("        DDRMEMCFG0=0x%08X  DDRREFCFG=0x%08X  DDRXFERCFG=0x%08X\r\n",
            (unsigned)DDRMEMCFG0, (unsigned)DDRREFCFG, (unsigned)DDRXFERCFG);
    printf("        DDRDLYCFG 0=0x%08X 1=0x%08X 2=0x%08X 3=0x%08X\r\n",
            (unsigned)DDRDLYCFG0, (unsigned)DDRDLYCFG1, (unsigned)DDRDLYCFG2, (unsigned)DDRDLYCFG3);
    printf("        DDRPHYPADCON=0x%08X\r\n", (unsigned)DDRPHYPADCON);
    // SCL block + read-capture DLL registers, for self-calibration debugging.
    // DDRPHYCLKDLY mirrors the SCL pass status (bits 4:3) and holds the SCL-
    // computed capture delta in CLKDLYDELTA (bits 2:0); a railed 0x7 delta
    // with pass bits 0 means SCL swept the whole range without a valid eye.
    printf("        DDRSCLLAT=0x%08X  DDRSCLCFG0=0x%08X  DDRSCLCFG1=0x%08X\r\n",
            (unsigned)DDRSCLLAT, (unsigned)DDRSCLCFG0, (unsigned)DDRSCLCFG1);
    printf("        DDRPHYDLLR=0x%08X  DDRADLLBYP=0x%08X  DDRPHYCLKDLY=0x%08X\r\n",
            (unsigned)DDRPHYDLLR, (unsigned)DDRADLLBYP, (unsigned)DDRPHYCLKDLY);

    // ---- Bring-up error flags ----
    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    Bring-up Errors:\r\n");

    if (error_handler.flags.DDR2_mpll_vreg_timeout ||
            error_handler.flags.DDR2_mpll_lock_timeout ||
            error_handler.flags.DDR2_init_sequence_timeout ||
            error_handler.flags.DDR2_calibration_timeout) {

        terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
        if (error_handler.flags.DDR2_mpll_vreg_timeout)
            printf("        MPLL voltage regulator ready timeout\r\n");
        if (error_handler.flags.DDR2_mpll_lock_timeout)
            printf("        MPLL lock timeout\r\n");
        if (error_handler.flags.DDR2_init_sequence_timeout)
            printf("        Init command sequence timeout\r\n");
        if (error_handler.flags.DDR2_calibration_timeout)
            printf("        Self-calibration timeout\r\n");

    }

    else {

        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("        None\r\n");

    }

    terminalTextAttributesReset();

}

/* *****************************************************************************
 End of File
 */
