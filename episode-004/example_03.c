/**
 * example_03.c - attached sprite demonstration
 * This example program displays a 15 color sprite (mandarin goby) in a fishtank
 */
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

#define IMG_FILENAME_PAL "fishtank_320x256x3.ts"
#define IMG_FILENAME_NTSC "fishtank_320x200x3.ts"

// playfield control
// single playfield, 3 bitplanes (8 colors)
#define BPLCON0_VALUE (0x3200)
// We have single playfield, so priority is determined in bits
// 5-3 and we need to set the playfield 2 priority bit (bit 6)
#define BPLCON2_VALUE (0x0048)

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
static UWORD __chip NULL_SPRITE_DATA[] = {
    0x0000, 0x0000,
    0x0000, 0x0000
};

static volatile ULONG *custom_vposr = (volatile ULONG *) 0xdff004;

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

static struct Ratr0TileSheet image;

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
    handler_info.is_Node.ln_Name = "nemo01";
    input_io->io_Command = IND_ADDHANDLER;
    input_io->io_Data = (APTR) &handler_info;
    DoIO((struct IORequest *) input_io);
    return 1;
}

UWORD sprite_palette[] = {
  0x0421, 0x0000, 0x0fff, 0x0d40, 0x025b, 0x047a, 0x0967, 0x0e20, 0x02ac, 0x088d, 0x07ad, 0x0ba7, 0x0aba, 0x07cb, 0x04dc, 0x07d9
};

UWORD __chip sprdata0[] = {
  0x002a, 0x0080,
  0x0000, 0x0000,
  0x0000, 0x0000,
  0x0000, 0x0000,
  0x0000, 0x0000,
  0x0000, 0x0000,
  0x0034, 0x000a,
  0x001a, 0x0004,
  0x00fd, 0x0028,
  0x0066, 0x0042,
  0x03f5, 0x00c1,
  0x01e3, 0x0103,
  0x00b4, 0x0034,
  0x0030, 0x0140,
  0x0240, 0x00c0,
  0x3d01, 0x0e01,
  0x721e, 0x711e,
  0x45fb, 0x4500,
  0x19fc, 0x1982,
  0x3330, 0x3300,
  0x0600, 0x0600,
  0x0000, 0x0000,
  0x0000, 0x0000
};

UWORD __chip sprdata1[] = {
  0x002a, 0x0080,
  0x0000, 0x0000,
  0x0000, 0x0000,
  0x0000, 0x0000,
  0x0000, 0x0000,
  0x0070, 0x0000,
  0x00f4, 0x000a,
  0x01ff, 0x0040,
  0x03c7, 0x0018,
  0x073d, 0x0080,
  0x0532, 0x024c,
  0x06e4, 0x0018,
  0x07cb, 0x0270,
  0x007c, 0x0183,
  0x02c1, 0x013e,
  0x7b1e, 0x00e0,
  0xff61, 0x0080,
  0xfb7e, 0x0081,
  0xee7c, 0x0082,
  0x7e08, 0x0038,
  0x7e00, 0x0000,
  0x1e00, 0x0000,
  0x0000, 0x0000
};

UWORD __chip sprdata2[] = {
  0x002a, 0x0080,
  0x0000, 0x0000,
  0x0000, 0x0000,
  0x0000, 0x0000,
  0x2000, 0x1000,
  0x5000, 0x2000,
  0xc000, 0x0000,
  0xb100, 0x0700,
  0x3d60, 0x0958,
  0x5afc, 0x5ad8,
  0xdafe, 0xcae6,
  0x0a7f, 0x0afe,
  0x093c, 0x0d5d,
  0x04b8, 0x02b8,
  0xcd40, 0xcf00,
  0xa300, 0x9c00,
  0x0580, 0x8400,
  0x0a00, 0x0a00,
  0x2a00, 0x0a00,
  0x0400, 0x0400,
  0x0c00, 0x0200,
  0x0000, 0x0000,
  0x0000, 0x0000
};

UWORD __chip sprdata3[] = {
  0x002a, 0x0080,
  0xc000, 0x0000,
  0x2000, 0x0000,
  0x3000, 0x0000,
  0x2800, 0x1000,
  0x5c00, 0x2000,
  0xfe00, 0x0000,
  0xf400, 0x3b00,
  0xf6e0, 0xbc78,
  0xadfc, 0x58e0,
  0x3de6, 0x18c2,
  0xddff, 0x28e7,
  0x3efc, 0xec7f,
  0xff78, 0x3638,
  0x32e0, 0x3fe0,
  0x4f80, 0x7000,
  0x3b80, 0x8000,
  0x3580, 0x0080,
  0x3580, 0x0080,
  0x1a00, 0x0100,
  0x0c00, 0x0200,
  0x0000, 0x0000,
  0x0000, 0x0000
};

static void set_sprite_pos(UWORD *sprite_data, UWORD hstart, UWORD vstart, UWORD vstop)
{
    sprite_data[0] = ((vstart & 0xff) << 8) | ((hstart >> 1) & 0xff);
    // vstop + high bit of vstart + low bit of hstart
    sprite_data[1] = ((vstop & 0xff) << 8) |  // vstop 8 low bits
        ((vstart >> 8) & 1) << 2 |  // vstart high bit
        ((vstop >> 8) & 1) << 1 |   // vstop high bit
        (hstart & 1) |              // hstart low bit
        sprite_data[1] & 0x80;      // new for attached sprites: preserve attach bit
}

static void cleanup(void)
{
    cleanup_input_handler();
    ratr0_free_tilesheet_data(&image);
    reset_display();
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
    vb_waitpos = is_pal ? 303 : 262;  // line to wait for vertical blanking
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

    // Set SPRITE DATA START
    // now point sprite 0 to the fish's left half data and sprite 1 to the right half
    coplist[COPLIST_IDX_SPR0_PTH_VALUE] = (((ULONG) sprdata0) >> 16) & 0xffff;
    coplist[COPLIST_IDX_SPR0_PTH_VALUE + 2] = ((ULONG) sprdata0) & 0xffff;
    coplist[COPLIST_IDX_SPR0_PTH_VALUE + 4] = (((ULONG) sprdata1) >> 16) & 0xffff;
    coplist[COPLIST_IDX_SPR0_PTH_VALUE + 6] = ((ULONG) sprdata1) & 0xffff;

    coplist[COPLIST_IDX_SPR0_PTH_VALUE + 8] = (((ULONG) sprdata2) >> 16) & 0xffff;
    coplist[COPLIST_IDX_SPR0_PTH_VALUE + 10] = ((ULONG) sprdata2) & 0xffff;
    coplist[COPLIST_IDX_SPR0_PTH_VALUE + 12] = (((ULONG) sprdata3) >> 16) & 0xffff;
    coplist[COPLIST_IDX_SPR0_PTH_VALUE + 14] = ((ULONG) sprdata3) & 0xffff;

    // set sprite colors
    for (int i = 1; i < 16; i++) {
        coplist[COPLIST_IDX_COLOR00_VALUE + ((16 + i) * 2)] = sprite_palette[i];
    }

    // and set the sprite position
    UWORD goby_x = 220, goby_y = 100, goby_height = 21;
    set_sprite_pos(sprdata0, goby_x, goby_y, goby_y + goby_height);
    set_sprite_pos(sprdata1, goby_x, goby_y, goby_y + goby_height);
    set_sprite_pos(sprdata2, goby_x + 16, goby_y, goby_y + goby_height);
    set_sprite_pos(sprdata3, goby_x + 16, goby_y, goby_y + goby_height);
    // SET SPRITE DATA END

    // initialize and activate the copper list
    custom.cop1lc = (ULONG) coplist;

    // the event loop
    while (!should_exit) {
        wait_vblank();
    }

    cleanup();
    return 0;
}
