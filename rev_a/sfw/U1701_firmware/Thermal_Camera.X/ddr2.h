/* ************************************************************************** */
/** DDR2 SDRAM Controller Driver

  @File Name
    ddr2.h

  @Summary
    Driver for the PIC32MZ2064DAR176's stacked-in-package 32MB DDR2 SDRAM

  @Description
    The PIC32MZ2064DAR176 is a "DA" (Graphics) family part with a 32MB DDR2
    SDRAM die stacked inside the same package (a package-on-package/embedded
    arrangement) -- there is no external SDRAM on this board, and no external
    signals to route or terminate. It is organized as 4,194,304 locations x 4
    banks x 16 bits (13 row, 9 column, 2 bank address bits, one Chip Select),
    per the PIC32MZ Graphics (DAK/DAL/DAR/DAS) Family data sheet (DS60001565),
    section 4.2.

    The DDR2 controller/PHY register set is documented in Section 55 "DDR
    SDRAM Controller" (DS60001321) of the PIC32 Family Reference Manual. That
    document describes the register fields and the JEDEC-standard init
    sequence in general terms but, because it's shared across every PIC32
    part with any DDR controller, does not give numeric values for this
    device's specific memory geometry, clocking or timing -- those are
    derived below from the DS60001565 timing table (44-55) and the standard
    JEDEC DDR2 (JESD79-2F) Mode/Extended Mode Register encodings, both cited
    inline. Register field names below (e.g. DDRMEMCFGx, DDRDLYCFGx,
    DDRCMD1x/DDRCMD2x) match the family reference manual and device header.

    Unlike most peripherals on this device, DDR2 is clocked from its own
    dedicated PLL (MPLL, via CFGMPLL) rather than SYSCLK/PBCLK -- ddr2.c
    configures and starts that PLL itself, independent of clockInitialize()
    in device_control.c.

    Once ddr2Initialize() completes, the SDRAM is simply mapped into the
    CPU's normal address space (see the physical/KSEG addresses below) --
    reads and writes to it are ordinary load/store instructions, not
    peripheral register accesses. ddr2Read()/ddr2Write() are provided as a
    bounds-checked, byte-addressable convenience wrapper around that memory,
    similar in spirit to memcpy(); direct pointer access via
    DDR2_KSEG1_BASE_ADDRESS (or DDR2_KSEG0_BASE_ADDRESS, see below) is just
    as valid for callers that want it, e.g. to place a large buffer or
    framebuffer there with a linker section.

    Cache note: DDR2_KSEG0_BASE_ADDRESS is the cached (KSEG0) alias and
    DDR2_KSEG1_BASE_ADDRESS is the uncached (KSEG1) alias of the same 32MB of
    physical memory. ddr2Read()/ddr2Write() use the uncached alias so every
    call is automatically coherent with no cache maintenance required. Code
    that accesses the cached alias directly for performance (e.g. a GLCD/GPU
    framebuffer) is responsible for its own cache writeback/invalidate.

    Several fields have no formula in the family reference manual and no
    device-specific recommendation in the data sheet (pad drive strength
    calibration, ODT read/write timing). Those are called out in ddr2.c and
    given conservative defaults; revisit them if signal integrity issues
    (data corruption under sustained access, sensitivity to temperature)
    show up during bring-up.
 */
/* ************************************************************************** */

#ifndef _DDR2_H    /* Guard against multiple inclusion */
#define _DDR2_H

#include <xc.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Physical address of the DDR2 SDRAM, and its size, per the PIC32MZ-DA
// family data sheet Table 4-1 "Address Mapping Table"
#define DDR2_PHYSICAL_BASE_ADDRESS     0x08000000
#define DDR2_SIZE_BYTES                0x02000000  // 32MB

// Cached (KSEG0) and uncached (KSEG1) virtual address aliases of the same
// physical DDR2 SDRAM -- see the cache note above
#define DDR2_KSEG0_BASE_ADDRESS        0x88000000
#define DDR2_KSEG1_BASE_ADDRESS        0xA8000000

// This function starts the dedicated Memory PLL (MPLL) to clock the DDR2
// PHY at 200MHz (the fastest this device's DDR2 supports), then runs the
// controller/PHY/SDRAM bring-up sequence. Blocks until the SDRAM is ready
// for normal reads/writes. Must run after PMDInitialize() has cleared
// PMD7bits.DDR2CMD (see power_saving.c) -- the controller is clock-gated
// off by default.
bool ddr2Initialize(void);

// This function returns true once ddr2Initialize() has completed and the
// SDRAM is ready for normal reads/writes (DDRMEMCONbits.INITDN)
bool ddr2IsReady(void);

// This function copies "length" bytes from DDR2 SDRAM starting at byte
// "offset" (0 = first byte of the 32MB) into "destination". Requests that
// run past the end of the 32MB are silently truncated to fit, matching
// this project's other bounds-checked helpers (e.g. printTimerStatus).
void ddr2Read(uint32_t offset, void *destination, uint32_t length);

// This function copies "length" bytes from "source" into DDR2 SDRAM
// starting at byte "offset". Requests that run past the end of the 32MB
// are silently truncated to fit.
void ddr2Write(uint32_t offset, const void *source, uint32_t length);

// This function runs a destructive read/write integrity test over the full
// 32MB of DDR2 (data-bus, address-bus, and full-array cell tests), printing a
// colored pass/fail per test and returning true only if all pass. WARNING: it
// overwrites all DDR2 contents -- do not call once anything is using it. See
// the definition in ddr2.c for details.
bool ddr2SelfTest(void);

// This function prints the DDR2 controller/PHY configuration and status
void printDDR2Status(void);

#endif /* _DDR2_H */

/* *****************************************************************************
 End of File
 */
