/**
 * example_01.c - area fill example
 * Demonstrate area fill with the blitter
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

#define IMG_FILENAME_PAL "shapes_320x256x2.ts"
#define IMG_FILENAME_NTSC "shapes_320x200x2.ts"

// playfield control
// single playfield, 2 bitplanes (4 colors)
#define BPLCON0_VALUE (0x2200)
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
#define COPLIST_IDX_BPL1PTH_VALUE (COPLIST_IDX_COLOR00_VALUE + 8)

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

    COP_MOVE(BPL1PTH, 0), COP_MOVE(BPL1PTL, 0),
    COP_MOVE(BPL2PTH, 0), COP_MOVE(BPL2PTL, 0),

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

static struct Ratr0TileSheet background;

// To handle input
static struct MsgPort *input_mp;
static struct IOStdReq *input_io;
static struct Interrupt handler_info;
static int should_exit;

#define SPACE (0x40)

// Area fill parameters
struct AreaFillParams {
    int x1, y1, x2, y2;
    UBYTE exclusive, fill_carry_input;

} fill_params[] = {
    { 0, 14, 63, 48, FALSE, 0 },
    { 64, 14, 127, 48, FALSE, 1 },
    { 128, 14, 191, 48, FALSE, 0 },
    { 192, 15, 255, 47, FALSE, 0 },

    { 0, 78, 63, 112, FALSE, 0 },
    { 64, 78, 127, 112, TRUE, 0 },
    { 128, 78, 191, 112, FALSE, 0 },
    { 192, 78, 255, 112, TRUE, 0 }
};
static int num_params = 8;
static int param_idx = 0;

static void area_fill(struct Ratr0TileSheet *background,
                      struct AreaFillParams *params);

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
    } else if (result->ie_Class == IECLASS_RAWKEY) {
        if (result->ie_Code == SPACE) {
            if (param_idx < num_params) {
                area_fill(&background, &fill_params[param_idx++]);
            }
        }
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
    handler_info.is_Node.ln_Name = "nemo01";
    input_io->io_Command = IND_ADDHANDLER;
    input_io->io_Data = (APTR) &handler_info;
    DoIO((struct IORequest *) input_io);
    return 1;
}

static void cleanup(void)
{
    cleanup_input_handler();
    ratr0_free_tilesheet_data(&background);
    reset_display();
}


/*
  Use area fill in the specified rectangular region. It does this
  by copying the area to itself using D = A in descending mode,
  where src A is the background image itself. Set the fill bits
  to specify the fill operation

  Note: fill comes after shift, mask and logical operations, so
  we can't mask out the fill
*/
static void area_fill(struct Ratr0TileSheet *background,
                      struct AreaFillParams *params)
{
    // determine the left and right borders, which are at the
    // word boundaries to the left and right sides
    int left = params->x1  - (params->x1 & 0x0f);
    int right = params->x2 + 16 - (params->x2 & 0x0f);
    int blit_width_pixels = right - left;
    int blit_height = (params->y2 - params->y1) + 1;
    int num_words = blit_width_pixels / 16;
    UBYTE fill_mode = params->exclusive ? 16 : 8;

    WaitBlit();
    custom.bltafwm = 0xffff;
    custom.bltalwm = 0xffff;

    custom.bltcon0 = 0x09f0;       // enable channels A and D, LF => D = A
    // descending mode + fill parameters
    custom.bltcon1 = fill_mode | (params->fill_carry_input << 2) | 0x2;

    // modulos are in bytes
    UWORD bltmod = (background->header.width - blit_width_pixels) / 8;
    // the address of source A and D has to be the word that defines the right
    // bottom corner
    UBYTE *src = background->imgdata + (params->y2 * background->header.width / 8) +
	right / 8 - 2;

    custom.bltdpt = src;
    custom.bltapt = src;
    custom.bltdmod = bltmod;
    custom.bltamod = bltmod;

    custom.bltsize = (blit_height << 6) | (num_words & 0x3f);
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

    OwnBlitter();
    // no sprite DMA
    custom.dmacon  = 0x0020;
    // initialize and activate the copper list
    custom.cop1lc = (ULONG) coplist;

    // the event loop
    while (!should_exit) {
        wait_vblank();
    }
    DisownBlitter();

    cleanup();
    return 0;
}
