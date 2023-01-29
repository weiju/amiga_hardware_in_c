/**
 * example_01.c - blitter object / cookie cut example
 * Demonstrate blitting masked object
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hardware/custom.h>
#include <clib/exec_protos.h>
#include <clib/intuition_protos.h>
#include <clib/graphics_protos.h>

#include <clib/alib_protos.h>
#include <devices/input.h>

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

#define IMG_FILENAME_PAL "grid_320x256x4.ts"
#define IMG_FILENAME_NTSC "grid_320x200x4.ts"

// playfield control
// single playfield, 4 bitplanes (16 colors)
#define BPLCON0_VALUE (0x4200)
// We have single playfield, so priority is determined in bits
// 5-3 and we need to set the playfield 2 priority bit (bit 6)
#define BPLCON2_VALUE (0x0048)

// copper instruction macros
#define COP_MOVE(addr, data) addr, data
#define COP_WAIT_END  0xffff, 0xfffe

// copper list indexes
#define COPLIST_IDX_DIWSTOP_VALUE (9)
#define COPLIST_IDX_BPL1MOD_VALUE (COPLIST_IDX_DIWSTOP_VALUE + 6)
#define COPLIST_IDX_BPL2MOD_VALUE (COPLIST_IDX_BPL1MOD_VALUE + 2)
#define COPLIST_IDX_COLOR00_VALUE (COPLIST_IDX_BPL2MOD_VALUE + 2)
#define COPLIST_IDX_BPL1PTH_VALUE (COPLIST_IDX_COLOR00_VALUE + 32)

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

    COP_MOVE(BPL1PTH, 0), COP_MOVE(BPL1PTL, 0),
    COP_MOVE(BPL2PTH, 0), COP_MOVE(BPL2PTL, 0),
    COP_MOVE(BPL3PTH, 0), COP_MOVE(BPL3PTL, 0),
    COP_MOVE(BPL4PTH, 0), COP_MOVE(BPL4PTL, 0),

    COP_WAIT_END,
    COP_WAIT_END
};

static volatile ULONG *custom_vposr = (volatile ULONG *) 0xdff004;

// Wait for this position for vertical blank
// translated from http://eab.abime.net/showthread.php?t=51928
static vb_waitpos;

static void wait_vblank()
{
    while (((*custom_vposr) & 0x1ff00) != (vb_waitpos<<8)) ;
}

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

static struct Ratr0TileSheet background, bobs;

// To handle input
static struct MsgPort *input_mp;
static struct IOStdReq *input_io;
static struct Interrupt handler_info;
static int should_exit;

static struct InputEvent *my_input_handler(__reg("a0") struct InputEvent *event,
                                           __reg("a1") APTR handler_data)
{
    struct InputEvent *result = event, *prev = NULL;

    Forbid();
    // Intercept all raw mouse events before they reach Intuition, ignore
    // everything else
    if (result->ie_Class == IECLASS_RAWMOUSE) {
        if (result->ie_Code == IECODE_LBUTTON) {
            should_exit = 1;
        }
        return NULL;
    }
    Permit();
    return result;
}

static void cleanup_input_handler(void)
{
    if (input_io) {
        // remove our input handler from the chain
        input_io->io_Command = IND_REMHANDLER;
        input_io->io_Data = (APTR) &handler_info;
        DoIO((struct IORequest *) input_io);

        if (!(CheckIO((struct IORequest *) input_io))) AbortIO((struct IORequest *) input_io);
        WaitIO((struct IORequest *) input_io);
        CloseDevice((struct IORequest *) input_io);
        DeleteExtIO((struct IORequest *) input_io);
    }
    if (input_mp) DeletePort(input_mp);
}

static BYTE error;

static int setup_input_handler(void)
{
    input_mp = CreatePort(0, 0);
    input_io = (struct IOStdReq *) CreateExtIO(input_mp, sizeof(struct IOStdReq));
    error = OpenDevice("input.device", 0L, (struct IORequest *) input_io, 0);

    handler_info.is_Code = (void (*)(void)) my_input_handler;
    handler_info.is_Data = NULL;
    handler_info.is_Node.ln_Pri = 100;
    handler_info.is_Node.ln_Name = "blitter02";
    input_io->io_Command = IND_ADDHANDLER;
    input_io->io_Data = (APTR) &handler_info;
    DoIO((struct IORequest *) input_io);
    return 1;
}

static void cleanup(void)
{
    cleanup_input_handler();
    ratr0_free_tilesheet_data(&bobs);
    ratr0_free_tilesheet_data(&background);
    reset_display();
}

#define SHIFT_PADDING (16)
/*
  Blit function where we can blit an aligned source to anywhere in the
  destination. This function assumes that the BOB sheet is set up in
  a way that of the tile width, the first 16 pixels are empty and provided
  as padding for shifting.

  Parameters:
  - bobs: a tile sheet that is assumed to include an additional mask plane
  - background is the background image to blit into
  - tilex, tiley: rather than pixel positions, this is the tile position
    within the bobs and masks sheets
  - dstx, dsty: The target coordinates to blit the object to within the
    background image

  Note:
  we won't handle clipping at the boundaries to keep things simple
*/
static void blit_object(struct Ratr0TileSheet *bobs,
			struct Ratr0TileSheet *background,
			int tilex, int tiley,
			int dstx, int dsty)
{
    // actual object width (without the padding)
    int tile_width_pixels = bobs->header.tile_width - SHIFT_PADDING;

    // this tile's x-position relative to the word containing it
    int tile_x0 = bobs->header.tile_width * tilex & 0x0f;

    // 1. determine how wide the blit actually is
    int blit_width = tile_width_pixels / 16;

    // width not a multiple of 16 ? -> add 1 to the width
    if (tile_width_pixels & 0x0f) blit_width++;

    int blit_width0_pixels = blit_width * 16;  // blit width in pixels

    // Final source blit width: does the tile extend into an additional word ?
    int src_blit_width = blit_width;
    if (tile_x0 > blit_width0_pixels - tile_width_pixels) src_blit_width++;

    // 2. Determine the amount of shift and the first word in the
    // destination
    int dst_x0 = dstx & 0x0f;  // destination x relative to the containing word
    int dst_shift = dst_x0 - tile_x0;  // shift amount
    int dst_blit_width = blit_width;
    int dst_offset = 0;

    // negative shift => shift is to the left, so we extend the shift to the
    // left and right-shift in the previous word so we always right-shift
    if (dst_shift < 0) {
        dst_shift = 16 + dst_shift;
        dst_blit_width++;
        dst_offset = -2;
    }

    // make the blit wider if it needs more space
    if (dst_x0 > blit_width0_pixels - tile_width_pixels) {
        dst_blit_width++;
    }

    UWORD alwm = 0xffff;
    int final_blit_width = src_blit_width;

    // due to relative positioning and shifts, the destination blit width
    // can be larger than the source blit, so we use the larger of the 2
    // and mask out last word of the source
    if (dst_blit_width > src_blit_width) {
        final_blit_width = dst_blit_width;
        alwm = 0;
    }

    WaitBlit();

    custom.bltafwm = 0xffff;
    custom.bltalwm = alwm;

    // cookie cut enable channels B, C and D, LF => D = AB + ~AC => 0xca
    // A = Mask sheet
    // B = Tile sheet
    // C = Background
    // D = Background
    custom.bltcon0 = 0x0fca | (dst_shift << 12);
    custom.bltcon1 = dst_shift << 12;  // shift in B

    // modulos are in bytes
    UWORD srcmod = bobs->header.width / 8 - (final_blit_width * 2);
    UWORD dstmod = background->header.width / 8 - (final_blit_width * 2);
    custom.bltamod = srcmod;
    custom.bltbmod = srcmod;
    custom.bltcmod = dstmod;
    custom.bltdmod = dstmod;

    // The blit size is the size of a plane of the tile size (1 word * 16)
    UWORD bltsize = ((bobs->header.tile_height) << 6) |
        (final_blit_width & 0x3f);

    // map the tile position to physical coordinates in the tile sheet
    int srcx = tilex * bobs->header.tile_width;
    int srcy = tiley * bobs->header.tile_height;

    int bobs_plane_size = bobs->header.width / 8 * bobs->header.height;
    int bg_plane_size = background->header.width / 8 * background->header.height;

    UBYTE *src = bobs->imgdata + srcy * bobs->header.width / 8 + srcx / 8;
    // The mask data is the plane after the source image planes
    UBYTE *mask = bobs->imgdata + bobs_plane_size * bobs->header.bmdepth +
        srcy * bobs->header.width / 8 + srcx / 8;
    UBYTE *dst = background->imgdata + dsty * background->header.width / 8 +
        dstx / 8 + dst_offset;

    for (int i = 0; i < bobs->header.bmdepth; i++) {

        custom.bltapt = mask;
        custom.bltbpt = src;
        custom.bltcpt = dst;
        custom.bltdpt = dst;
        custom.bltsize = bltsize;

        // Increase the pointers to the next plane
        src += bobs_plane_size;
        dst += bg_plane_size;

        WaitBlit();
    }
}

int main(int argc, char **argv)
{
    if (!setup_input_handler()) {
        puts("Could not initialize input handler");
        return 1;
    }
    SetTaskPri(FindTask(NULL), TASK_PRIORITY);
    BOOL is_pal = init_display();
    const char *bgfile = is_pal ? IMG_FILENAME_PAL : IMG_FILENAME_NTSC;
    if (!ratr0_read_tilesheet(bgfile, &background)) {
        puts("Could not read background image");
        cleanup();
        return 1;
    }
    if (!ratr0_read_tilesheet("rodland_bobs.ts", &bobs)) {
        puts("Could not read bob sheet");
        cleanup();
        return 1;
    }

    if (is_pal) {
        coplist[COPLIST_IDX_DIWSTOP_VALUE] = DIWSTOP_VALUE_PAL;
        vb_waitpos = 303;
    } else {
        coplist[COPLIST_IDX_DIWSTOP_VALUE] = DIWSTOP_VALUE_NTSC;
        vb_waitpos = 262;
    }

    int img_row_bytes = background.header.width / 8;
    UBYTE num_colors = 1 << background.header.bmdepth;

    // 1. copy the background palette to the copper list
    for (int i = 0; i < num_colors; i++) {
        coplist[COPLIST_IDX_COLOR00_VALUE + (i << 1)] = background.palette[i];
    }

    // 2. prepare background bitplanes and point the copper list entries
    // to the bitplanes. The data is non-interleaved.
    int coplist_idx = COPLIST_IDX_BPL1PTH_VALUE;
    int plane_size = background.header.height * img_row_bytes;
    ULONG addr;
    for (int i = 0; i < background.header.bmdepth; i++) {
        addr = (ULONG) &(background.imgdata[i * plane_size]);
        coplist[coplist_idx] = (addr >> 16) & 0xffff;
        coplist[coplist_idx + 2] = addr & 0xffff;
        coplist_idx += 4; // next bitplane
    }

    // no sprite DMA
    custom.dmacon  = 0x0020;

    OwnBlitter();
    blit_object(&bobs, &background, 1, 0, 40, 58);
    blit_object(&bobs, &background, 0, 0, 18, 5);
    blit_object(&bobs, &background, 2, 0, 83, 77);
    blit_object(&bobs, &background, 3, 0, 163, 155);
    DisownBlitter();

    // initialize and activate the copper list
    custom.cop1lc = (ULONG) coplist;

    // the event loop
    while (!should_exit) {
        wait_vblank();
    }

    cleanup();
    return 0;
}
