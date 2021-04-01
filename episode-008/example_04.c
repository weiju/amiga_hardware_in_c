/**
 * example_04.c - horizontal scrolling example (advanced)
 * Horizontal scrolling with a tile map
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

#define DIWSTRT_VALUE      0x2c81
#define DIWSTOP_VALUE_PAL  0x2cc1
#define DIWSTOP_VALUE_NTSC 0xf4c1

// Data fetch (horizontal scroll, DDFSTRT is 8 clocks earlier)
#define DDFSTRT_VALUE      0x0030
#define DDFSTOP_VALUE      0x00d0

// playfield control
// single playfield, 5 bitplanes (32 colors)
#define BPLCON0_VALUE (0x5200)
// We have single playfield, so priority is determined in bits
// 5-3 and we need to set the playfield 2 priority bit (bit 6)
#define BPLCON2_VALUE (0x0048)

// copper instruction macros
#define COP_MOVE(addr, data) addr, data
#define COP_WAIT_END  0xffff, 0xfffe

// copper list indexes
#define COPLIST_IDX_DIWSTOP_VALUE (9)
#define COPLIST_IDX_BPLCON1_VALUE (COPLIST_IDX_DIWSTOP_VALUE + 4)
#define COPLIST_IDX_BPL1MOD_VALUE (COPLIST_IDX_DIWSTOP_VALUE + 8)
#define COPLIST_IDX_BPL2MOD_VALUE (COPLIST_IDX_BPL1MOD_VALUE + 2)
#define COPLIST_IDX_COLOR00_VALUE (COPLIST_IDX_BPL2MOD_VALUE + 2)
#define COPLIST_IDX_BPL1PTH_VALUE (COPLIST_IDX_COLOR00_VALUE + 64)


// Interleaved playfield values
#define NUM_BITPLANES     (5)
#define SCREEN_WIDTH      (704)
#define SCREEN_WIDTH_PER_HALF (352)
#define BYTES_PER_ROW     (SCREEN_WIDTH / 8)
#define NUM_ROWS          (256)
#define BPL_MODULO        ((SCREEN_WIDTH - 320) / 8 + SCREEN_WIDTH / 8 * 4 - 2)
#define VTILES            (256 / 16)
#define HTILES_PER_HALF   (352 / 16)
#define HTILES_TOTAL      (704 / 16)
#define DMOD              (BYTES_PER_ROW -  2)
#define PLANE_SIZE        (BYTES_PER_ROW * NUM_ROWS)

static UWORD __chip coplist[] = {
    COP_MOVE(FMODE,   0), // set fetch mode = 0

    COP_MOVE(DDFSTRT, DDFSTRT_VALUE),
    COP_MOVE(DDFSTOP, DDFSTOP_VALUE),
    COP_MOVE(DIWSTRT, DIWSTRT_VALUE),
    COP_MOVE(DIWSTOP, DIWSTOP_VALUE_PAL),
    COP_MOVE(BPLCON0, BPLCON0_VALUE),
    COP_MOVE(BPLCON1, 0),
    COP_MOVE(BPLCON2, BPLCON2_VALUE),
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

    COP_MOVE(BPL1PTH, 0), COP_MOVE(BPL1PTL, 0),
    COP_MOVE(BPL2PTH, 0), COP_MOVE(BPL2PTL, 0),
    COP_MOVE(BPL3PTH, 0), COP_MOVE(BPL3PTL, 0),
    COP_MOVE(BPL4PTH, 0), COP_MOVE(BPL4PTL, 0),
    COP_MOVE(BPL5PTH, 0), COP_MOVE(BPL5PTL, 0),

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

static UBYTE __chip *display_buffer;

//static struct Ratr0TileSheet background;
static struct Ratr0TileSheet tileset;
static struct Ratr0Level level;

// To handle input
static struct MsgPort *input_mp;
static struct IOStdReq *input_io;
static struct Interrupt handler_info;
static int should_exit;

#define ESCAPE       (0x45)

static struct InputEvent *my_input_handler(__reg("a0") struct InputEvent *event,
                                           __reg("a1") APTR handler_data)
{
    struct InputEvent *result = event, *prev = NULL;

    Forbid();
    // Intercept all raw key events before they reach Intuition, ignore
    // everything else
    if (result->ie_Class == IECLASS_RAWKEY) {
        if (result->ie_Code == ESCAPE) {
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
    handler_info.is_Node.ln_Name = "scrolling";
    input_io->io_Command = IND_ADDHANDLER;
    input_io->io_Data = (APTR) &handler_info;
    DoIO((struct IORequest *) input_io);
    return 1;
}

static void cleanup(void)
{
    cleanup_input_handler();
    ratr0_free_level_data(&level);
    ratr0_free_tilesheet_data(&tileset);
    reset_display();
}

void blit_column(UBYTE *dst, int column)
{
    UBYTE *curr_dst = dst;
    int tilenum, tx, ty;
    int lx = column;

    for (int ly = 0; ly < VTILES; ly++) {
        tilenum = level.lvldata[ly * level.header.width + lx] - 1;
        tx = tilenum % tileset.header.num_tiles_h;
        ty = tilenum / tileset.header.num_tiles_h;
        ratr0_blit_tile(curr_dst, DMOD, &tileset, tx, ty);
        curr_dst += BYTES_PER_ROW * tileset.header.tile_height * tileset.header.bmdepth;
    }
}

#define MAX_X_OFFSET (352)
#define MIN_X_POS    (0)
#define MAX_X_POS    (544)
#define SPEED (1)

int main(int argc, char **argv)
{
    if (!setup_input_handler()) {
        puts("Could not initialize input handler");
        return 1;
    }
    SetTaskPri(FindTask(NULL), TASK_PRIORITY);
    BOOL is_pal = init_display();

    // same vertical size for PAL and NTSC
    int display_buffer_size = BYTES_PER_ROW * NUM_ROWS * NUM_BITPLANES;
    display_buffer = AllocMem(display_buffer_size, MEMF_CHIP|MEMF_CLEAR);

    if (!ratr0_read_tilesheet("graphics/rocknroll_tiles.ts", &tileset)) {
        puts("Could not read tile set");
        cleanup();
        return 1;
    }
    if (!ratr0_read_level("graphics/rocknroll_horizontal.lvl", &level)) {
        puts("Could not read level");
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

    UBYTE num_colors = 1 << tileset.header.bmdepth;

    // 1. copy the background palette to the copper list
    for (int i = 0; i < num_colors; i++) {
        coplist[COPLIST_IDX_COLOR00_VALUE + (i << 1)] = tileset.palette[i];
    }

    // 2. prepare background bitplanes and point the copper list entries
    // to the bitplanes. The data is non-interleaved.
    int coplist_idx = COPLIST_IDX_BPL1PTH_VALUE;

    ULONG addr = (ULONG) display_buffer;
    for (int i = 0; i < 5; i++) {
        coplist[coplist_idx] = (addr >> 16) & 0xffff;
        coplist[coplist_idx + 2] = addr & 0xffff;
        coplist_idx += 4; // next bitplane
        addr += BYTES_PER_ROW;
    }
    OwnBlitter();

    // Blit left half
    for (int lx = 0; lx < HTILES_PER_HALF; lx++) {
        blit_column(display_buffer + lx * 2, lx);
    }

    /* Blit right half so we add half the screen width to the destination */
    for (int lx = 0; lx < HTILES_PER_HALF; lx++) {
        blit_column(display_buffer + (HTILES_PER_HALF + lx) * 2, lx);
    }

    // no sprite DMA
    custom.dmacon  = 0x0020;
    // initialize and activate the copper list
    custom.cop1lc = (ULONG) coplist;

    // the event loop
    int xpos = MIN_X_POS;  // logical x position (relative to the level)
    int x_offset = MIN_X_POS;  // physical x position (relative to the display buffer)
    int x_inc = SPEED;

    int num_pixels_shift, num_words_skip, blit_left, blit_right;
    UWORD delay_mask = 0;

    while (!should_exit) {
        wait_vblank();

        blit_left = blit_right = 0;

        // calculate the current pointers and delay values
        num_words_skip = x_offset / 16;
        num_pixels_shift = 16 - (x_offset % 16);
        if (num_pixels_shift == 16) {
            num_words_skip--;
            num_pixels_shift = 0;
            blit_left = 1;
        } else if (num_pixels_shift == 8) {
            blit_right = 1;
        }
        delay_mask = (num_pixels_shift << 4) | num_pixels_shift;
        coplist[COPLIST_IDX_BPLCON1_VALUE] = delay_mask;

        // update bitmap pointer
        coplist_idx = COPLIST_IDX_BPL1PTH_VALUE;
        addr = (ULONG) display_buffer + num_words_skip * 2;
        for (int i = 0; i < NUM_BITPLANES; i++) {
            coplist[coplist_idx] = (addr >> 16) & 0xffff;
            coplist[coplist_idx + 2] = addr & 0xffff;
            coplist_idx += 4;
            addr += BYTES_PER_ROW;
        }

        xpos += x_inc;
        if (xpos <= MIN_X_POS) {
            xpos = MIN_X_POS;
            x_inc = SPEED;
        } else if (xpos >= MAX_X_POS) {
            xpos = MAX_X_POS;
            x_inc = -SPEED;
        }

        // display buffer position, adjusted to the size
        x_offset = xpos % SCREEN_WIDTH_PER_HALF;

        if (blit_left || blit_right) {
            // blit incoming column
            int curr_level_col = xpos / 16;
            int curr_screen_col = x_offset / 16;
            int left_col_offset = -1;
            int right_col_offset = HTILES_PER_HALF - 1;
            int level_col = x_inc > 0 ? curr_level_col + right_col_offset  // scroll left -> add from the right
                : curr_level_col + left_col_offset;  // scroll right -> add from the left

            if (blit_left) {
                // if the display window is all the way to the right, we blit the right column at
                // the left of the display buffer
                int left_col = curr_screen_col + left_col_offset;
                if (left_col < 0) left_col = HTILES_TOTAL + left_col;
                blit_column(display_buffer + left_col * 2, level_col);
            }
            if (blit_right) {
                // if the display window is all the way to the left, we blit the left column at the
                // right side of the display buffer
                int right_col = curr_screen_col + right_col_offset;
                if (right_col >= HTILES_TOTAL) right_col = right_col - HTILES_TOTAL + 1;
                blit_column(display_buffer + right_col * 2, level_col);
            }
        }
    }
    DisownBlitter();
    FreeMem(display_buffer, display_buffer_size);
    cleanup();
    return 0;
}
