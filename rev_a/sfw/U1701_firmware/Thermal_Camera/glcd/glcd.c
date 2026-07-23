/*******************************************************************************
  Graphics LCD (GLCD) Controller Driver

  File Name:
    glcd.c

  Summary:
    Register-level driver for the on-die GLCD Controller. See glcd.h for
    the layering rationale and clock/framebuffer design notes.

  CRITICAL ACCESS-SIZE CONSTRAINT (found the hard way, 2026-07-19):
    The GLCD register block (0xBF8EA000+) responds to sub-word (8/16-bit)
    accesses with a bus error -- the CPU takes a Data Bus Error exception
    (EXCCODE 7) with nothing latched in the System Bus error logs. XC32
    compiles volatile bitfield accesses (GLCDxxxbits.FIELD) down to the
    smallest container that covers the field, so an innocent-looking
    "GLCDCLKCONbits.CLKDIV = 32" became an lhu/sh halfword pair and
    bus-faulted on the halfword LOAD (verified by disassembling the ELF at
    the exception's EPC). Full-word (lw/sw) accesses work fine.

    Therefore: NEVER use the GLCDxxxbits structs in this file, for reads or
    writes. Every GLCD register access below is a full 32-bit register
    assignment/read, composed with the header's _GLCDx_FIELD_POSITION/_MASK
    macros. (Microchip's Harmony plib_glcd.c does the same -- full-word
    masked writes only -- which is why this constraint isn't mentioned
    anywhere in the datasheet or FRM.) Non-GLCD registers (CFGCON2,
    REFO5CON, PMD6) are normal SFRs and don't have this constraint.
*******************************************************************************/

#include <xc.h>
#include <stdio.h>
#include <string.h>
#include <sys/kmem.h>

#include "glcd/glcd.h"
#include "glcd/device_driver/glt035320240is1.h"
#include "core/device_control.h"
#include "core/ddr2.h"
#include "gpio/pin_macros.h"
#include "usb_uart/terminal_control.h"

// GLCDLxMODE.COLORMODE encodings (PIC32 Family Reference Manual Register
// 54-9). RGB888 matches Layer 0's frame buffer layout and the panel's
// native 24-bit interface; ARGB8888 (0xAARRGGBB, one byte per channel with
// alpha in the top byte) matches Layer 1's overlay buffers and LVGL's
// LV_COLOR_FORMAT_ARGB8888.
//
// UNVERIFIED ON HARDWARE: the numeric encodings come from Microchip's
// Harmony plib_glcd.h, not from the datasheet, the family reference manual
// or the XC32 device header (none of which enumerate this field). If the
// overlay's reds and blues come out swapped, the fix is almost certainly to
// use RGBA8888 (0x2) here instead.
#define GLCD_COLORMODE_RGB888     0xBu
#define GLCD_COLORMODE_ARGB8888   0x6u

// Layer blend functions (GLCDLxMODE SRCBLEND<11:8>/DESTBLEND<15:12>):
// standard source-over compositing, matching Microchip's Harmony reference
// driver for this silicon (drv_gfx_glcd.c: SRC_BLEND_ALPHA_SRCGBL,
// DEST_BLEND_INV_SRCGBL). With layer ALPHA=0xFF this renders the layer
// fully opaque over the background color; chosen now (instead of the
// blend-black POR default 0/0, which would render everything black) so the
// upcoming test-image step displays correctly.
#define GLCD_SRCBLEND_ALPHA_SRCGBL  0x4u
#define GLCD_DESTBLEND_INV_SRCGBL   0x7u

// The X/Y register pairs (GLCDRES, GLCDxPORCH, GLCDBLANKING, GLCDL0START/
// SIZE/RES) all place X in <26:16> and Y in <10:0>
#define GLCD_XY(x, y)   (((uint32_t)(x) << 16) | (uint32_t)(y))

bool GLCD_Initialize(void)
{
    if (PMD6bits.GLCDMD)
    {
        // PMD is one-shot (application/power_saving.c) -- if it's still
        // set here, PMDInitialize() didn't clear it (or wasn't called yet)
        // and every GLCD SFR access below would be inert
        return false;
    }

    // 24-bit RGB888 pin mode (GLCDMODE=0; the schematic wires the full
    // R0-7/G0-7/B0-7 bus) and hand the GDx/sync/clock pads to the GLCD
    // module (GLCDPINEN)
    CFGCON2bits.GLCDMODE = 0;
    CFGCON2bits.GLCDPINEN = 1;

    // Make sure the controller is disabled while timing/layers are
    // programmed (all-zero is also the POR value of GLCDMODE)
    GLCDMODE = 0;

    // Pixel clock: GCLK = REFCLKO5 / CLKDIV. REFCLK5 is already running at
    // SYSCLK undivided = 200MHz (core/device_control.c's REFCLK5Initialize(),
    // called unconditionally by clockInitialize() at boot). The divider
    // value and its derivation live with the rest of the clock settings in
    // core/device_control.h. LPREFETCH keeps its POR value of 4 (16 lines
    // prefetched by the layer DMA before frame start).
    GLCDCLKCON = (4u << _GLCDCLKCON_LPREFETCH_POSITION)
               | ((uint32_t)GLCD_PIXEL_CLOCK_DIVIDER << _GLCDCLKCON_CLKDIV_POSITION);

    // Background color behind/around the layer: opaque black
    GLCDBGCOLOR = 0;

    // Video timing -- see glt035320240is1.h for the derivation from the
    // panel datasheet's timing table
    GLCDRES      = GLCD_XY(GLT035320240IS1_WIDTH_PX, GLT035320240IS1_HEIGHT_PX);
    GLCDFPORCH   = GLCD_XY(GLT035320240IS1_FPORCHX,   GLT035320240IS1_FPORCHY);
    GLCDBLANKING = GLCD_XY(GLT035320240IS1_BLANKINGX, GLT035320240IS1_BLANKINGY);
    GLCDBPORCH   = GLCD_XY(GLT035320240IS1_BPORCHX,   GLT035320240IS1_BPORCHY);

    // Layer 0: the single, full-screen, opaque RGB888 layer backing the
    // frame buffer. See glcd.h for why only one layer is used.
    GLCDL0START  = GLCD_XY(0, 0);
    GLCDL0SIZE   = GLCD_XY(GLCD_FRAMEBUFFER_WIDTH_PX, GLCD_FRAMEBUFFER_HEIGHT_PX);
    GLCDL0RES    = GLCD_XY(GLCD_FRAMEBUFFER_WIDTH_PX, GLCD_FRAMEBUFFER_HEIGHT_PX);
    GLCDL0STRIDE = GLCD_FRAMEBUFFER_STRIDE_BYTES;
    // GLCDLxBADDR wants the physical address: the GLCD Controller's DMA is
    // a separate DDR2 bus master (core/ddr2.h file header), not a CPU
    // load/store through a KSEG alias, so the KSEG1 pointer must be
    // converted with KVA_TO_PA() first.
    GLCDL0BADDR = KVA_TO_PA(GLCD_FRAMEBUFFER_BASE_ADDRESS);
    GLCDL0MODE = _GLCDL0MODE_LAYEREN_MASK
               | (0xFFu << _GLCDL0MODE_ALPHA_POSITION)
               | (GLCD_DESTBLEND_INV_SRCGBL << _GLCDL0MODE_DESTBLEND_POSITION)
               | (GLCD_SRCBLEND_ALPHA_SRCGBL << _GLCDL0MODE_SRCBLEND_POSITION)
               | (GLCD_COLORMODE_RGB888 << _GLCDL0MODE_COLORMODE_POSITION);

    // Blank the frame buffer (black, matching this panel's Normally-Black
    // mode) through the uncached KSEG1 alias -- coherent with the GLCD
    // Controller's DMA with no cache maintenance needed (core/ddr2.h cache
    // note). Filling it with real image data is a separate, later step.
    memset((void *)GLCD_FRAMEBUFFER_BASE_ADDRESS, 0, GLCD_FRAMEBUFFER_SIZE_BYTES);

    // Panel reset sequence before the controller starts driving timing
    // signals at it
    GLT035320240IS1_ResetPulse();

    // Signal polarity: not specified for this panel in its datasheet (see
    // glt035320240is1.h) -- common ST7272A default: active-low HSYNC/VSYNC
    // (POL bits set = negative), active-high DE, data latched on the rising
    // DCLK edge (DEPOL/PCLKPOL clear = positive). RGBSEQ=0 = parallel RGB.
    // Written once without LCDEN, then again with LCDEN, so the controller
    // enables with its final configuration already in place.
    uint32_t glcdmode_config = _GLCDMODE_HSYNCPOL_MASK | _GLCDMODE_VSYNCPOL_MASK;
    GLCDMODE = glcdmode_config;
    GLCDMODE = glcdmode_config | _GLCDMODE_LCDEN_MASK;

    return true;
}

bool GLCD_OverlayInitialize(void)
{
    if ((GLCDMODE & _GLCDMODE_LCDEN_MASK) == 0)
    {
        // GLCD_Initialize() hasn't run (or failed): timing isn't programmed
        // and the panel isn't being driven, so an enabled Layer 1 would
        // composite over nothing
        return false;
    }

    // Both buffers start fully transparent (alpha = 0 in every pixel, which
    // for ARGB8888 means all-zero) so that whichever one is scanned out
    // before the GUI has drawn anything shows Layer 0 unmodified.
    memset((void *)GLCD_OVERLAY_BUFFER_A_ADDRESS, 0, GLCD_OVERLAY_SIZE_BYTES);
    memset((void *)GLCD_OVERLAY_BUFFER_B_ADDRESS, 0, GLCD_OVERLAY_SIZE_BYTES);

    GLCDL1START  = GLCD_XY(0, 0);
    GLCDL1SIZE   = GLCD_XY(GLCD_OVERLAY_WIDTH_PX, GLCD_OVERLAY_HEIGHT_PX);
    GLCDL1RES    = GLCD_XY(GLCD_OVERLAY_WIDTH_PX, GLCD_OVERLAY_HEIGHT_PX);
    GLCDL1STRIDE = GLCD_OVERLAY_STRIDE_BYTES;

    // Start on buffer B; gui/lv_port_disp.c renders its first frame into
    // buffer A, off-screen, and flips to it when that frame is complete.
    GLCD_SetOverlayBaseAddress((const void *)GLCD_OVERLAY_BUFFER_B_ADDRESS);

    // Same source-over blend as Layer 0 (see the constants above) -- with
    // ARGB8888 the per-pixel alpha drives it, so a transparent pixel shows
    // Layer 0 and an opaque one hides it. Layer ALPHA stays 0xFF (a global
    // scale on top of the per-pixel alpha) and MULALPHA stays 0 (the buffer
    // is not premultiplied; LVGL renders straight ARGB).
    GLCDL1MODE = _GLCDL1MODE_LAYEREN_MASK
               | (0xFFu << _GLCDL1MODE_ALPHA_POSITION)
               | (GLCD_DESTBLEND_INV_SRCGBL << _GLCDL1MODE_DESTBLEND_POSITION)
               | (GLCD_SRCBLEND_ALPHA_SRCGBL << _GLCDL1MODE_SRCBLEND_POSITION)
               | (GLCD_COLORMODE_ARGB8888 << _GLCDL1MODE_COLORMODE_POSITION);

    return true;
}

bool GLCD_Layer2Initialize(void)
{
    if ((GLCDMODE & _GLCDMODE_LCDEN_MASK) == 0)
    {
        // GLCD_Initialize() hasn't run: no timing programmed, nothing to
        // composite over.
        return false;
    }

    // Clear to black so a stale image never flashes when the layer is enabled
    // before new content is written.
    memset((void *)GLCD_LAYER2_BASE_ADDRESS, 0, GLCD_LAYER2_SIZE_BYTES);

    GLCDL2START  = GLCD_XY(0, 0);
    GLCDL2SIZE   = GLCD_XY(GLCD_LAYER2_WIDTH_PX, GLCD_LAYER2_HEIGHT_PX);
    GLCDL2RES    = GLCD_XY(GLCD_LAYER2_WIDTH_PX, GLCD_LAYER2_HEIGHT_PX);
    GLCDL2STRIDE = GLCD_LAYER2_STRIDE_BYTES;
    GLCDL2BADDR  = KVA_TO_PA(GLCD_LAYER2_BASE_ADDRESS);

    // Same opaque source-over blend as Layer 0, but LAYEREN starts CLEAR so the
    // layer is invisible until GLCD_Layer2SetEnabled(true). Full-word write
    // per the GLCD sub-word bus-error rule (see the file header).
    GLCDL2MODE = (0xFFu << _GLCDL2MODE_ALPHA_POSITION)
               | (GLCD_DESTBLEND_INV_SRCGBL << _GLCDL2MODE_DESTBLEND_POSITION)
               | (GLCD_SRCBLEND_ALPHA_SRCGBL << _GLCDL2MODE_SRCBLEND_POSITION)
               | (GLCD_COLORMODE_RGB888 << _GLCDL2MODE_COLORMODE_POSITION);

    return true;
}

void GLCD_Layer2SetEnabled(bool enabled)
{
    // Full-word read-modify-write of just the LAYEREN bit.
    uint32_t mode = GLCDL2MODE;

    if (enabled)
    {
        mode |= _GLCDL2MODE_LAYEREN_MASK;
    }
    else
    {
        mode &= ~_GLCDL2MODE_LAYEREN_MASK;
    }

    GLCDL2MODE = mode;
}

bool GLCD_Layer2IsEnabled(void)
{
    return (GLCDL2MODE & _GLCDL2MODE_LAYEREN_MASK) != 0;
}

void GLCD_SetOverlayBaseAddress(const void *buffer)
{
    // Physical address, for the same reason as Layer 0's GLCDL0BADDR above:
    // the layer DMA is a separate DDR2 bus master, not a CPU KSEG access.
    GLCDL1BADDR = KVA_TO_PA((uint32_t)buffer);
}

bool GLCD_WaitOverlayVSync(void)
{
    // CP0 Count runs at SYSCLK/2. Two frame periods at this panel's ~60Hz
    // is a generous bound: whatever the phase when this is called, a healthy
    // panel enters vertical blanking within one frame.
    uint32_t start = _CP0_GET_COUNT();
    uint32_t timeout_ticks = (uint32_t)(SYSCLK_INT / 2u) / 30u;

    // Already blanking: wait it out first, otherwise a caller that arrives
    // late in the blanking interval could flip just as scanout resumes
    while (GLCDSTAT & _GLCDSTAT_VSYNC_MASK)
    {
        if ((_CP0_GET_COUNT() - start) > timeout_ticks) return false;
    }

    while ((GLCDSTAT & _GLCDSTAT_VSYNC_MASK) == 0)
    {
        if ((_CP0_GET_COUNT() - start) > timeout_ticks) return false;
    }

    return true;
}

// Derives the actual output GCLK frequency from REFCLKO5 (per
// core/device_control.c's REFCLK5Initialize()/printClockStatus() formula:
// REFCLKO5 = SYSCLK/(2*RODIV) for RODIV != 0, else SYSCLK passthrough) and
// GLCDCLKCON.CLKDIV.
static uint32_t GLCD_DeriveGclkHz(void)
{
    uint32_t refclko5_hz = REFO5CONbits.RODIV
            ? (uint32_t)SYSCLK_INT / (2u * REFO5CONbits.RODIV)
            : (uint32_t)SYSCLK_INT;

    uint32_t clkdiv = (GLCDCLKCON & _GLCDCLKCON_CLKDIV_MASK) >> _GLCDCLKCON_CLKDIV_POSITION;

    return clkdiv ? (refclko5_hz / clkdiv) : refclko5_hz;
}

void GLCD_PrintStatus(void)
{
    // Full-word register snapshots -- see the file header for why the
    // GLCDxxxbits structs must not be used even for reads
    uint32_t glcdmode     = GLCDMODE;
    uint32_t glcdclkcon   = GLCDCLKCON;
    uint32_t glcdres      = GLCDRES;
    uint32_t glcdfporch   = GLCDFPORCH;
    uint32_t glcdblanking = GLCDBLANKING;
    uint32_t glcdbporch   = GLCDBPORCH;
    uint32_t glcdl0mode   = GLCDL0MODE;
    uint32_t glcdl0size   = GLCDL0SIZE;
    uint32_t glcdl0stride = GLCDL0STRIDE;
    uint32_t glcdl0baddr  = GLCDL0BADDR;
    uint32_t glcdl1mode   = GLCDL1MODE;
    uint32_t glcdl1size   = GLCDL1SIZE;
    uint32_t glcdl1stride = GLCDL1STRIDE;
    uint32_t glcdl1baddr  = GLCDL1BADDR;
    uint32_t glcdl2mode   = GLCDL2MODE;
    uint32_t glcdl2size   = GLCDL2SIZE;
    uint32_t glcdl2baddr  = GLCDL2BADDR;

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, BOLD_FONT);
    printf("    --- GLCD Controller ---\n\r");

    if (PMD6bits.GLCDMD) terminalTextAttributes(RED_COLOR, BLACK_COLOR, NORMAL_FONT);
    else terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    PMD Gating: %s\n\r", PMD6bits.GLCDMD ? "disabled (GLCDMD=1)" : "enabled");

    terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    LCD Controller Enabled (GLCDMODE.LCDEN): %s\n\r",
            (glcdmode & _GLCDMODE_LCDEN_MASK) ? "yes" : "no");
    printf("    Pin Mode (CFGCON2): GLCDPINEN=%u, %s GD bus\n\r",
            (unsigned int)CFGCON2bits.GLCDPINEN, CFGCON2bits.GLCDMODE ? "16-bit" : "24-bit");
    printf("    Resolution: %lux%lu\n\r",
            (unsigned long)(glcdres >> 16) & 0x7FFu, (unsigned long)(glcdres & 0x7FFu));
    printf("    Front Porch (X/Y): %lu/%lu\n\r",
            (unsigned long)(glcdfporch >> 16) & 0x7FFu, (unsigned long)(glcdfporch & 0x7FFu));
    printf("    Blanking (X/Y): %lu/%lu\n\r",
            (unsigned long)(glcdblanking >> 16) & 0x7FFu, (unsigned long)(glcdblanking & 0x7FFu));
    printf("    Back Porch (X/Y): %lu/%lu\n\r",
            (unsigned long)(glcdbporch >> 16) & 0x7FFu, (unsigned long)(glcdbporch & 0x7FFu));
    printf("    Polarity: HSYNCPOL=%u VSYNCPOL=%u DEPOL=%u PCLKPOL=%u (1=negative)\n\r",
            (unsigned int)((glcdmode & _GLCDMODE_HSYNCPOL_MASK) != 0),
            (unsigned int)((glcdmode & _GLCDMODE_VSYNCPOL_MASK) != 0),
            (unsigned int)((glcdmode & _GLCDMODE_DEPOL_MASK) != 0),
            (unsigned int)((glcdmode & _GLCDMODE_PCLKPOL_MASK) != 0));
    printf("    Pixel Clock: REFO5CON.ON=%u RODIV=%u GLCDCLKCON.CLKDIV=%lu -> GCLK ~= %lu Hz\n\r",
            (unsigned int)REFO5CONbits.ON, (unsigned int)REFO5CONbits.RODIV,
            (unsigned long)(glcdclkcon & _GLCDCLKCON_CLKDIV_MASK),
            (unsigned long)GLCD_DeriveGclkHz());

    printf("    Layer 0: Enabled=%u ColorMode=0x%lX Alpha=0x%02lX Size=%lux%lu Stride=%luB\n\r",
            (unsigned int)((glcdl0mode & _GLCDL0MODE_LAYEREN_MASK) != 0),
            (unsigned long)(glcdl0mode & _GLCDL0MODE_COLORMODE_MASK),
            (unsigned long)((glcdl0mode & _GLCDL0MODE_ALPHA_MASK) >> _GLCDL0MODE_ALPHA_POSITION),
            (unsigned long)(glcdl0size >> 16) & 0x7FFu, (unsigned long)(glcdl0size & 0x7FFu),
            (unsigned long)(glcdl0stride & 0xFFFFu));
    printf("    Layer 0 Base Address (physical): 0x%08lX\n\r", (unsigned long)glcdl0baddr);
    printf("    Frame Buffer (CPU, KSEG1 uncached): 0x%08lX, %lu bytes\n\r",
            (unsigned long)GLCD_FRAMEBUFFER_BASE_ADDRESS, (unsigned long)GLCD_FRAMEBUFFER_SIZE_BYTES);

    // Layer 1 is only enabled once the GUI is running (GLCD_OverlayInitialize())
    if (glcdl1mode & _GLCDL1MODE_LAYEREN_MASK)
    {
        printf("    Layer 1 (GUI overlay): Enabled=1 ColorMode=0x%lX Alpha=0x%02lX Size=%lux%lu Stride=%luB\n\r",
                (unsigned long)(glcdl1mode & _GLCDL1MODE_COLORMODE_MASK),
                (unsigned long)((glcdl1mode & _GLCDL1MODE_ALPHA_MASK) >> _GLCDL1MODE_ALPHA_POSITION),
                (unsigned long)(glcdl1size >> 16) & 0x7FFu, (unsigned long)(glcdl1size & 0x7FFu),
                (unsigned long)(glcdl1stride & 0xFFFFu));
        printf("    Layer 1 Base Address (physical): 0x%08lX (buffer %s)\n\r",
                (unsigned long)glcdl1baddr,
                (glcdl1baddr == KVA_TO_PA(GLCD_OVERLAY_BUFFER_A_ADDRESS)) ? "A" :
                (glcdl1baddr == KVA_TO_PA(GLCD_OVERLAY_BUFFER_B_ADDRESS)) ? "B" : "?");
        printf("    Overlay Buffers (CPU, KSEG1 uncached): A 0x%08lX, B 0x%08lX, %lu bytes each\n\r",
                (unsigned long)GLCD_OVERLAY_BUFFER_A_ADDRESS,
                (unsigned long)GLCD_OVERLAY_BUFFER_B_ADDRESS,
                (unsigned long)GLCD_OVERLAY_SIZE_BYTES);
    }
    else
    {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    Layer 1 (GUI overlay): disabled (GUI not initialized)\n\r");
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    }

    // Layer 2 is the on-demand still-image layer (application/image_loader.c),
    // disabled unless an image is currently being shown.
    if (glcdl2mode & _GLCDL2MODE_LAYEREN_MASK)
    {
        printf("    Layer 2 (still image): Enabled=1 ColorMode=0x%lX Size=%lux%lu Base(phys)=0x%08lX\n\r",
                (unsigned long)(glcdl2mode & _GLCDL2MODE_COLORMODE_MASK),
                (unsigned long)(glcdl2size >> 16) & 0x7FFu, (unsigned long)(glcdl2size & 0x7FFu),
                (unsigned long)glcdl2baddr);
    }
    else
    {
        terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
        printf("    Layer 2 (still image): disabled (no image shown)\n\r");
        terminalTextAttributes(GREEN_COLOR, BLACK_COLOR, NORMAL_FONT);
    }

    terminalTextAttributes(YELLOW_COLOR, BLACK_COLOR, NORMAL_FONT);
    printf("    LCD_ENABLE_PIN (panel RESET, RJ11): %s\n\r", LCD_ENABLE_PIN ? "high (released)" : "low (in reset)");

    terminalTextAttributesReset();
}
