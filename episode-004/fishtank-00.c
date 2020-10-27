#include <stdio.h>
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

#define IMG_FILENAME_PAL "fishtank_320x256.ts"
#define IMG_FILENAME_NTSC "fishtank_320x200.ts"

// playfield control
// bplcon0: use bitplane 1-5 = BPU 101, composite color enable
//#define BPLCON0_VALUE (0x5200)
// single playfield, 3 bitplanes (8 colors)
#define BPLCON0_VALUE (0x3200)
// We have single playfield, so priority is determined in bits
// 5-3 and we need to set the playfield 2 priority bit (bit 6)
#define BPLCON2_VALUE (0x0048)
//#define BPLCON2_VALUE (0x0040)

// copper instruction macros
#define COP_MOVE(addr, data) addr, data
#define COP_WAIT_END  0xffff, 0xfffe

// copper list indexes
#define COPLIST_IDX_SPR0_PTH_VALUE (3)
#define COPLIST_IDX_DIWSTOP_VALUE (COPLIST_IDX_SPR0_PTH_VALUE + 32 + 6)
#define COPLIST_IDX_BPL1MOD_VALUE (COPLIST_IDX_DIWSTOP_VALUE + 6)
#define COPLIST_IDX_BPL2MOD_VALUE (COPLIST_IDX_BPL1MOD_VALUE + 2)
#define COPLIST_IDX_COLOR00_VALUE (COPLIST_IDX_BPL2MOD_VALUE + 2)
#define COPLIST_IDX_BPL1PTH_VALUE (COPLIST_IDX_COLOR00_VALUE + 64)
#define COPLIST_IDX_BPL1PTH_SECOND_VALUE (COPLIST_IDX_BPL1PTH_VALUE + 20 + 4)

static UWORD __chip coplist[] = {
    COP_MOVE(FMODE,   0), // set fetch mode = 0

    // sprites first
    COP_MOVE(SPR0PTH, 0), COP_MOVE(SPR0PTL, 0),
    COP_MOVE(SPR1PTH, 0), COP_MOVE(SPR1PTL, 0),
    COP_MOVE(SPR2PTH, 0), COP_MOVE(SPR2PTL, 0),
    COP_MOVE(SPR3PTH, 0), COP_MOVE(SPR3PTL, 0),
    COP_MOVE(SPR4PTH, 0), COP_MOVE(SPR4PTL, 0),
    COP_MOVE(SPR5PTH, 0), COP_MOVE(SPR5PTL, 0),
    COP_MOVE(SPR6PTH, 0), COP_MOVE(SPR6PTL, 0),
    COP_MOVE(SPR7PTH, 0), COP_MOVE(SPR7PTL, 0),

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


volatile ULONG *custom_vposr = (volatile ULONG *) 0xdff004;

static void wait_vblank()
{
    // translated from http://eab.abime.net/showthread.php?t=51928
    // 303 -> 263 for NTSC
    while (((*custom_vposr) & 0x1ff00) != (303<<8)) ;
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

static struct Ratr0TileSheet image;

// To handle input
struct MsgPort *input_mp;
struct IOStdReq *input_io;
struct Interrupt handler_info;
BYTE error = 0;
int should_exit = 0;

struct InputEvent *my_input_handler(__reg("a0") struct InputEvent *event,
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

void cleanup_input_handler(void)
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

int setup_input_handler()
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

int main(int argc, char **argv)
{
    if (!setup_input_handler()) {
        puts("Could not initialize input handler");
        return 1;
    }
    SetTaskPri(FindTask(NULL), TASK_PRIORITY);
    BOOL is_pal = init_display();
    const char *bgfile = is_pal ? IMG_FILENAME_PAL : IMG_FILENAME_NTSC;
    if (!ratr0_read_tilesheet(bgfile, &image)) {
        puts("Could not read background image");
        return 1;
    }

    if (is_pal) {
        coplist[COPLIST_IDX_DIWSTOP_VALUE] = DIWSTOP_VALUE_PAL;
    } else {
        coplist[COPLIST_IDX_DIWSTOP_VALUE] = DIWSTOP_VALUE_NTSC;
    }
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

    // 3. prepare bitplanes and point the copper list entries
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

    // point sprites 0-7 to nothing
    for (int i = 0; i < 8; i++) {
        coplist[COPLIST_IDX_SPR0_PTH_VALUE + i * 4] = (((ULONG) NULL_SPRITE_DATA) >> 16) & 0xffff;
        coplist[COPLIST_IDX_SPR0_PTH_VALUE + i * 4 + 2] = ((ULONG) NULL_SPRITE_DATA) & 0xffff;
    }

    // initialize and activate the copper list
    custom.cop1lc = (ULONG) coplist;

    // the event loop
    int dir = 1, incx = 0;

    while (!should_exit) {
        wait_vblank();
    }

    // cleanup
    cleanup_input_handler();

    ratr0_free_tilesheet_data(&image);
    reset_display();
    return 0;
}
