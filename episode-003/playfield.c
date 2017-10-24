#include <stdio.h>
#include <hardware/custom.h>
#include <clib/exec_protos.h>
#include <clib/intuition_protos.h>
#include <clib/graphics_protos.h>
#include <graphics/gfxbase.h>
#include <ahpc_registers.h>

#include "tilesheet.h"

extern struct GfxBase *GfxBase;
extern struct Custom custom;

// 20 instead of 127 because of input.device priority
#define TASK_PRIORITY           (20)
#define PRA_FIR0_BIT            (1 << 6)


// Data fetch
#define DDFSTRT_VALUE      0x0038
#define DDFSTOP_VALUE      0x00d0
#define DIWSTRT_VALUE      0x2c81
#define DIWSTOP_VALUE_PAL  0x2cc1
#define DIWSTOP_VALUE_NTSC 0xf4c1

// Display dimensions and data size
#define DISPLAY_WIDTH    (320)
#define DISPLAY_HEIGHT   (200)
#define DISPLAY_ROW_BYTES (DISPLAY_WIDTH / 8)
#define PAL_PLANE_SIZE   (DISPLAY_ROW_BYTES * 256)
#define NTSC_PLANE_SIZE   (DISPLAY_ROW_BYTES * 200)
#define NUM_BITPLANES    (5)
#define PAL_DISPLAY_SIZE   ((DISPLAY_ROW_BYTES * 256) * NUM_BITPLANES )
#define NTSC_DISPLAY_SIZE   ((DISPLAY_ROW_BYTES * 200) * NUM_BITPLANES)

#ifdef INTERLEAVED
#define BPL_MODULO ((NUM_BITPLANES - 1) * DISPLAY_ROW_BYTES)
#define IMG_FILE_NAME "gorilla-interleaved.img"
#else
#define BPL_MODULO (0)
#define IMG_FILE_NAME "gorilla-noninterleaved.img"
#endif

// playfield control
// bplcon0: use bitplane 1-5 = BPU 101, composite color enable
// bplcon1: horizontal scroll value = 0 for all playfields
#define BPLCON0_VALUE (0x5200)
#define BPLCON1_VALUE (0)

// copper instruction macros
#define COP_MOVE(addr, data) addr, data
#define COP_WAIT_END  0xffff, 0xfffe

// copper list indexes
#define COPLIST_IDX_DIWSTOP_VALUE (9)
#define COPLIST_IDX_COLOR00_VALUE (19)
#define COPLIST_IDX_BPL1PTH_VALUE (19 + 64)

static UWORD __chip coplist[] = {
    COP_MOVE(FMODE,   0), // set fetch mode = 0
    COP_MOVE(DDFSTRT, DDFSTRT_VALUE),
    COP_MOVE(DDFSTOP, DDFSTOP_VALUE),
    COP_MOVE(DIWSTRT, DIWSTRT_VALUE),
    COP_MOVE(DIWSTOP, DIWSTOP_VALUE_PAL),
    COP_MOVE(BPLCON0, BPLCON0_VALUE),
    COP_MOVE(BPLCON1, BPLCON1_VALUE),
    COP_MOVE(BPL1MOD, BPL_MODULO),
    COP_MOVE(BPL2MOD, BPL_MODULO),

    // set up the display colors
    COP_MOVE(COLOR00, 0x000), COP_MOVE(COLOR01, 0x000),
    COP_MOVE(COLOR02, 0x000), COP_MOVE(COLOR03, 0x000),
    COP_MOVE(COLOR04, 0x000), COP_MOVE(COLOR05, 0x000),
    COP_MOVE(COLOR06, 0x000), COP_MOVE(COLOR07, 0x000),
    COP_MOVE(COLOR08, 0x000), COP_MOVE(COLOR09, 0x000),
    COP_MOVE(COLOR10, 0x000), COP_MOVE(COLOR11, 0x000),
    COP_MOVE(COLOR12, 0x000), COP_MOVE(COLOR13, 0x000),
    COP_MOVE(COLOR14, 0x000), COP_MOVE(COLOR15, 0x000),
    COP_MOVE(COLOR16, 0x000), COP_MOVE(COLOR17, 0x000),
    COP_MOVE(COLOR18, 0x000), COP_MOVE(COLOR19, 0x000),
    COP_MOVE(COLOR20, 0x000), COP_MOVE(COLOR21, 0x000),
    COP_MOVE(COLOR22, 0x000), COP_MOVE(COLOR23, 0x000),
    COP_MOVE(COLOR24, 0x000), COP_MOVE(COLOR25, 0x000),
    COP_MOVE(COLOR26, 0x000), COP_MOVE(COLOR27, 0x000),
    COP_MOVE(COLOR28, 0x000), COP_MOVE(COLOR29, 0x000),
    COP_MOVE(COLOR30, 0x000), COP_MOVE(COLOR31, 0x000),

    COP_MOVE(BPL1PTH, 0),
    COP_MOVE(BPL1PTL, 0),
    COP_MOVE(BPL2PTH, 0),
    COP_MOVE(BPL2PTL, 0),
    COP_MOVE(BPL3PTH, 0),
    COP_MOVE(BPL3PTL, 0),
    COP_MOVE(BPL4PTH, 0),
    COP_MOVE(BPL4PTL, 0),
    COP_MOVE(BPL5PTH, 0),
    COP_MOVE(BPL5PTL, 0),
    COP_WAIT_END,
    COP_WAIT_END
};


static BOOL init_display(void)
{
    LoadView(NULL);  // clear display, reset hardware registers
    WaitTOF();       // 2 WaitTOFs to wait for 1. long frame and
    WaitTOF();       // 2. short frame copper lists to finish (if interlaced)
    return (((struct GfxBase *) GfxBase)->DisplayFlags & PAL) == PAL;
}

static void reset_display(void)
{
    LoadView(((struct GfxBase *) GfxBase)->ActiView);
    WaitTOF();
    WaitTOF();
    custom.cop1lc = (ULONG) ((struct GfxBase *) GfxBase)->copinit;
    RethinkDisplay();
}

static void waitmouse(void)
{
    volatile UBYTE *ciaa_pra = (volatile UBYTE *) 0xbfe001;
    while ((*ciaa_pra & PRA_FIR0_BIT) != 0) ;
}

static struct Ratr0TileSheet image;

int main(int argc, char **argv)
{
    SetTaskPri(FindTask(NULL), TASK_PRIORITY);
    BOOL is_pal = init_display();
    if (ratr0_read_tilesheet(IMG_FILE_NAME, &image, PAL_DISPLAY_SIZE)) {
        if (is_pal) {
            coplist[COPLIST_IDX_DIWSTOP_VALUE] = DIWSTOP_VALUE_PAL;
        } else {
            coplist[COPLIST_IDX_DIWSTOP_VALUE] = DIWSTOP_VALUE_NTSC;
        }
        UBYTE num_colors = 1 << image.header.bmdepth;

        // 1. copy the palette to the copper list
        for (int i = 0; i < num_colors; i++) {
            coplist[COPLIST_IDX_COLOR00_VALUE + (i << 1)] = image.palette[i];
        }
        // 2. prepare bitplanes and point the copper list entries
        // to the bitplanes (we already initialized the modulos statically)
        int coplist_idx = COPLIST_IDX_BPL1PTH_VALUE;
        for (int i = 0; i < image.header.bmdepth; i++) {
#ifdef INTERLEAVED
            ULONG addr = (ULONG) &(image.imgdata[i * DISPLAY_ROW_BYTES]);
#else
            ULONG addr = (ULONG) &(image.imgdata[i * image.header.height * DISPLAY_ROW_BYTES]);
#endif
            coplist[coplist_idx] = (addr >> 16) & 0xffff;
            coplist[coplist_idx + 2] = addr & 0xffff;
            coplist_idx += 4; // next bitplane
        }

        // 3. disable sprite DMA and initialize the copper list
        custom.dmacon  = 0x0020;
        custom.cop1lc = (ULONG) coplist;
        waitmouse();
        ratr0_free_tilesheet_data(&image);
    }
    reset_display();
    return 0;
}
