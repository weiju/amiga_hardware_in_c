#include <stdio.h>
#include <string.h>
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

#define IMG_FILE_NAME "fishtank.ts"
#define SPRITE_FILE_NAME "nemo16x8.ts"

// playfield control
// bplcon0: use bitplane 1-5 = BPU 101, composite color enable
#define BPLCON0_VALUE (0x5200)
// We have single playfield, so priority is determined in bits
// 5-3 and we need to set the playfield 2 priority bit (bit 6)
//#define BPLCON2_VALUE (0x0048)
#define BPLCON2_VALUE (0x0040)

// copper instruction macros
#define COP_MOVE(addr, data) addr, data
#define COP_WAIT_END  0xffff, 0xfffe

// copper list indexes
#define COPLIST_IDX_DIWSTOP_VALUE (9)
#define COPLIST_IDX_BPL1MOD_VALUE (COPLIST_IDX_DIWSTOP_VALUE + 6)
#define COPLIST_IDX_BPL2MOD_VALUE (COPLIST_IDX_BPL1MOD_VALUE + 2)
#define COPLIST_IDX_COLOR00_VALUE (COPLIST_IDX_BPL2MOD_VALUE + 2)
#define COPLIST_IDX_BPL1PTH_VALUE (COPLIST_IDX_COLOR00_VALUE + 64)
#define COPLIST_IDX_BPL1PTH_SECOND_VALUE (COPLIST_IDX_BPL1PTH_VALUE + 20 + 4)

// Sprite pointers start after the bitplane pointers
#define COPLIST_IDX_SPR0_PTH_VALUE (COPLIST_IDX_BPL1PTH_VALUE + 20)

static UWORD __chip coplist[] = {
    COP_MOVE(FMODE,   0), // set fetch mode = 0
    COP_MOVE(DDFSTRT, DDFSTRT_VALUE),
    COP_MOVE(DDFSTOP, DDFSTOP_VALUE),
    COP_MOVE(DIWSTRT, DIWSTRT_VALUE),
    COP_MOVE(DIWSTOP, DIWSTOP_VALUE_PAL),
    COP_MOVE(BPLCON0, BPLCON0_VALUE),
    COP_MOVE(BPLCON2, BPLCON2_VALUE),
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

    COP_MOVE(BPL1PTH, 0), COP_MOVE(BPL1PTL, 0),
    COP_MOVE(BPL2PTH, 0), COP_MOVE(BPL2PTL, 0),
    COP_MOVE(BPL3PTH, 0), COP_MOVE(BPL3PTL, 0),
    COP_MOVE(BPL4PTH, 0), COP_MOVE(BPL4PTL, 0),
    COP_MOVE(BPL5PTH, 0), COP_MOVE(BPL5PTL, 0),

    COP_MOVE(SPR0PTH, 0), COP_MOVE(SPR0PTL, 0),
    COP_MOVE(SPR1PTH, 0), COP_MOVE(SPR1PTL, 0),
    COP_MOVE(SPR2PTH, 0), COP_MOVE(SPR2PTL, 0),
    COP_MOVE(SPR3PTH, 0), COP_MOVE(SPR3PTL, 0),
    COP_MOVE(SPR4PTH, 0), COP_MOVE(SPR4PTL, 0),
    COP_MOVE(SPR5PTH, 0), COP_MOVE(SPR5PTL, 0),
    COP_MOVE(SPR6PTH, 0), COP_MOVE(SPR6PTL, 0),
    COP_MOVE(SPR7PTH, 0), COP_MOVE(SPR7PTL, 0),

    // change background color so it's not so plain
    0x5c07, 0xfffe,
    COP_MOVE(COLOR00, 0x237),
    0x9c07, 0xfffe,
    COP_MOVE(COLOR00, 0x236),
    0xda07, 0xfffe,
    COP_MOVE(COLOR00, 0x235),

    COP_WAIT_END,
    COP_WAIT_END
};

// null sprite data for sprites that are supposed to be inactive
UWORD __chip NULL_SPRITE_DATA[] = {
    0x0000, 0x0000,
    0x0000, 0x0000
};

// size = 4 words for control words 1+2 and 2 stop data data words
//        + <height> * 2 (bitplanes) words (2 * 8 = 16) of pixel data
#define NEMO_DATA_WORDS (4 + 16)
#define NEMO_HSTART (122)
#define NEMO_VSTART (213)
#define NEMO_VSTOP (NEMO_VSTART + 8)

UWORD __chip nemo_data[NEMO_DATA_WORDS];

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
static struct Ratr0TileSheet sprite;

void set_sprite_pos(UWORD *sprite_data, UWORD hstart, UWORD vstart, UWORD vstop)
{
    sprite_data[0] = ((vstart & 0xff) << 8) | (hstart & 0xff);
    // vstop + high bit of vstart + low bit of hstart
    sprite_data[1] = ((vstop & 0xff) << 8) |  // vstop 8 low bits
        ((vstart >> 8) & 1) << 2 |  // vstart high bit
        ((vstop >> 8) & 1) << 1 |   // vstop high bit
        (hstart & 1);  // hstart low bit
}

int main(int argc, char **argv)
{
    SetTaskPri(FindTask(NULL), TASK_PRIORITY);
    BOOL is_pal = init_display();
    ULONG retval;
    if (!ratr0_read_tilesheet(IMG_FILE_NAME, &image)) {
        puts("Could not read background image");
        return 1;
    }
    if (!ratr0_read_tilesheet(SPRITE_FILE_NAME, &sprite)) {
        puts("Could not read sprite image");
        ratr0_free_tilesheet_data(&image);
        return 1;
    }

    coplist[COPLIST_IDX_DIWSTOP_VALUE] = DIWSTOP_VALUE_PAL;
    int img_row_bytes = image.header.width / 8;
    UBYTE num_colors = 1 << image.header.bmdepth;

    // 1. adjust the bitplane modulos if interleaved
    int bplmod = (image.header.bmdepth - 1) * img_row_bytes;
    coplist[COPLIST_IDX_BPL1MOD_VALUE] = bplmod;
    coplist[COPLIST_IDX_BPL2MOD_VALUE] = bplmod;

    // 2. copy the background palette to the copper list
    for (int i = 0; i < num_colors; i++) {
        coplist[COPLIST_IDX_COLOR00_VALUE + (i << 1)] = image.palette[i];
    }
    // 3. copy the sprite palette to the copper list
    for (int i = 0; i < 4; i++) {
        coplist[COPLIST_IDX_COLOR00_VALUE + ((16 + i) << 1)] = sprite.palette[i];
    }
    // 4. prepare bitplanes and point the copper list entries
    // to the bitplanes
    int coplist_idx = COPLIST_IDX_BPL1PTH_VALUE;
    int coplist_split_idx = COPLIST_IDX_BPL1PTH_SECOND_VALUE;
    int plane_size = image.header.height * img_row_bytes;
    ULONG addr;
    for (int i = 0; i < image.header.bmdepth; i++) {
        addr = (ULONG) &(image.imgdata[i * img_row_bytes]);
        coplist[coplist_idx] = (addr >> 16) & 0xffff;
        coplist[coplist_idx + 2] = addr & 0xffff;
        coplist_idx += 4; // next bitplane
    }
    // now we are looking at the sprite pointers
    // setup the sprite data structure, control words + data words
    memset(nemo_data, 0, NEMO_DATA_WORDS * 2);
    set_sprite_pos(nemo_data, NEMO_HSTART, NEMO_VSTART, NEMO_VSTOP);

    // copy image data into sprite structure set sprite 0 pointer
    UBYTE *dst = (UBYTE *) &nemo_data[2];
    for (int i = 0; i < sprite.header.imgdata_size; i++) {
        dst[i] = sprite.imgdata[i];
    }
    coplist[COPLIST_IDX_SPR0_PTH_VALUE] = (((ULONG) nemo_data) >> 16) & 0xffff;
    coplist[COPLIST_IDX_SPR0_PTH_VALUE + 2] = ((ULONG) nemo_data) & 0xffff;
    // point sprites 1-7 to nothing
    for (int i = 1; i < 8; i++) {
        coplist[COPLIST_IDX_SPR0_PTH_VALUE + i * 4] = (((ULONG) NULL_SPRITE_DATA) >> 16) & 0xffff;
        coplist[COPLIST_IDX_SPR0_PTH_VALUE + i * 4 + 2] = ((ULONG) NULL_SPRITE_DATA) & 0xffff;
    }

    // initialize and activate the copper list
    custom.cop1lc = (ULONG) coplist;

    // the event loop
    waitmouse();

    // cleanup
    ratr0_free_tilesheet_data(&image);
    ratr0_free_tilesheet_data(&sprite);
    reset_display();
    return 0;
}
