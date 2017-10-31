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

#define DIWSTRT_VALUE      0x2c81
#define DIWSTOP_VALUE_PAL  0x2cc1
#define DIWSTOP_VALUE_NTSC 0xf4c1

// Data fetch
#define DDFSTRT_VALUE      0x0038
#define DDFSTOP_VALUE      0x00d0

// Display dimensions and data size
#define DISPLAY_WIDTH    (320)
#define DISPLAY_HEIGHT   (256)
#define DISPLAY_ROW_BYTES (DISPLAY_WIDTH / 8)

#ifdef INTERLEAVED
#define IMG_FILE_NAME "gorilla256-interleaved.img"
#else
#define IMG_FILE_NAME "gorilla256-noninterleaved.img"
#endif

// playfield control
// bplcon0: use bitplane 1-5 = BPU 101, composite color enable
#define BPLCON0_VALUE (0x5200)

// copper instruction macros
#define COP_MOVE(addr, data) addr, data
#define COP_WAIT_END  0xffff, 0xfffe

// copper list indexes
#define COPLIST_IDX_DIWSTOP_VALUE (9)
#define COPLIST_IDX_BPL1MOD_VALUE (13)
#define COPLIST_IDX_BPL2MOD_VALUE (15)
#define COPLIST_IDX_COLOR00_VALUE (17)
#define COPLIST_IDX_BPL1PTH_VALUE (17 + 64)

static UWORD __chip coplist[] = {
    COP_MOVE(FMODE,   0), // set fetch mode = 0
    COP_MOVE(DDFSTRT, DDFSTRT_VALUE),
    COP_MOVE(DDFSTOP, DDFSTOP_VALUE),
    COP_MOVE(DIWSTRT, DIWSTRT_VALUE),
    COP_MOVE(DIWSTOP, DIWSTOP_VALUE_PAL),
    COP_MOVE(BPLCON0, BPLCON0_VALUE),
    COP_MOVE(BPL1MOD, 0),
    COP_MOVE(BPL2MOD, 0),

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
    if (ratr0_read_tilesheet(IMG_FILE_NAME, &image)) {
        if (is_pal) {
            coplist[COPLIST_IDX_DIWSTOP_VALUE] = DIWSTOP_VALUE_PAL;
        } else {
            coplist[COPLIST_IDX_DIWSTOP_VALUE] = DIWSTOP_VALUE_NTSC;
        }
        int img_row_bytes = image.header.width / 8;
        UBYTE num_colors = 1 << image.header.bmdepth;

        // 1. adjust the bitplane modulos if interleaved
#ifdef INTERLEAVED
        int bplmod = (image.header.bmdepth - 1) * img_row_bytes;
        coplist[COPLIST_IDX_BPL1MOD_VALUE] = bplmod;
        coplist[COPLIST_IDX_BPL2MOD_VALUE] = bplmod;
#endif
        // 2. copy the palette to the copper list
        for (int i = 0; i < num_colors; i++) {
            coplist[COPLIST_IDX_COLOR00_VALUE + (i << 1)] = image.palette[i];
        }
        // 3. prepare bitplanes and point the copper list entries
        // to the bitplanes
        int coplist_idx = COPLIST_IDX_BPL1PTH_VALUE;
        int plane_size = image.header.height * img_row_bytes;
        ULONG addr;
        for (int i = 0; i < image.header.bmdepth; i++) {
#ifdef INTERLEAVED
            addr = (ULONG) &(image.imgdata[i * img_row_bytes]);
#else
            addr = (ULONG) &(image.imgdata[i * plane_size]);
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
