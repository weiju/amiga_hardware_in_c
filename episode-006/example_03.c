/**
 * example_03.c - filled polygon example
 * Combine line draw and area fill for filled polygons
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

#define IMG_FILENAME_PAL "grid64_320x256x3.ts"
#define IMG_FILENAME_NTSC "grid64_320x200x3.ts"

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
#define COPLIST_IDX_DIWSTOP_VALUE (9)
#define COPLIST_IDX_BPL1MOD_VALUE (COPLIST_IDX_DIWSTOP_VALUE + 6)
#define COPLIST_IDX_BPL2MOD_VALUE (COPLIST_IDX_BPL1MOD_VALUE + 2)
#define COPLIST_IDX_COLOR00_VALUE (COPLIST_IDX_BPL2MOD_VALUE + 2)
#define COPLIST_IDX_BPL1PTH_VALUE (COPLIST_IDX_COLOR00_VALUE + 16)

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

    COP_MOVE(BPL1PTH, 0), COP_MOVE(BPL1PTL, 0),
    COP_MOVE(BPL2PTH, 0), COP_MOVE(BPL2PTL, 0),
    COP_MOVE(BPL3PTH, 0), COP_MOVE(BPL3PTL, 0),

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
#define LF_COOKIE_CUT (0xca)
#define LF_XOR (0x4a)

struct DrawLineParams {
    UWORD x1, y1, x2, y2;
    int plane;
    UWORD line_pattern;
    UBYTE pattern_offset, lf_byte, single, omit_first_pixel;
};

struct AreaFillParams {
    int x1, y1, x2, y2;
    UBYTE exclusive, fill_carry_input;
};

struct TriangleParams {
    UWORD x1, y1, x2, y2, x3, y3;
    UBYTE filled;
} triangle_params[] = {
    // The points should be sorted by increasing y-coordinates
    { 96, 10, 120, 48, 70, 60, TRUE },
    { 142, 25, 132, 45, 165, 52, TRUE },
    { 248, 4, 204, 28, 221, 59, TRUE },
    { 261, 14, 310, 14, 268, 62, TRUE },
    { 57, 71, 3, 71, 63, 112, TRUE },
    { 115, 68, 69, 93, 121, 119, TRUE }
};

struct CopyTileParams {
    int srcx, srcy, dstx, dsty, color;
} copy_tile_params[] = {
    { 1, 0, 0, 0, 2 },
    { 2, 0, 0, 0, 3 },
    { 3, 0, 0, 0, 4 },
    { 4, 0, 0, 0, 5 },
    { 0, 1, 0, 0, 6 },
    { 1, 1, 0, 0, 7 }
};

int num_params = 12;
int param_idx = 0;

static void draw_line(struct Ratr0TileSheet *background, struct DrawLineParams *p);
static void area_fill(struct Ratr0TileSheet *background, struct AreaFillParams *params);
static void draw_triangle(struct Ratr0TileSheet *background, struct TriangleParams *params);
static void copy_tile(struct Ratr0TileSheet *background, struct CopyTileParams *params);

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
                if (param_idx % 2 == 0) {
                    draw_triangle(&background, &triangle_params[param_idx / 2]);
                } else {
                    copy_tile(&background, &copy_tile_params[param_idx / 2]);
                }
                param_idx++;
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

/**
 *  Draw a line assuming left top corner is at 0, 0 of the destination bit plane.
 */
// some empty memory to point the first line pixel to
static UWORD __chip scratchmem[12];
static void draw_line(struct Ratr0TileSheet *background, struct DrawLineParams *p)
{
    UWORD dx = abs(p->x2 - p->x1), dy = abs(p->y2 - p->y1), dmax, dmin;
    UWORD bytes_per_line = background->header.width / 8;

    // Determine the octant and therefore the code bits
    UBYTE code;
    if (p->y1 >= p->y2) {
        if (p->x1 <= p->x2) {
            code = dx >= dy ? 6 : 1;
        } else {
            code = dx <= dy ? 3 : 7;
        }
    } else {
        if (p->x1 >= p->x2) {
            code = dx >= dy ? 5 : 2;
        } else {
            code = dx <= dy ? 0 : 4;
        }
    }

    if (dx <= dy) {
        dmin = dx;
        dmax = dy;
    } else {
        dmin = dy;
        dmax = dx;
    }
    WORD aptlval = 4 * dmin - 2 * dmax;
    UWORD startx = (p->x1 & 0xf) << 12;  // x1 modulo 16
    UWORD texture = ((p->x1 + p->pattern_offset) & 0xf) << 12;  // BSH in BLTCON1
    UWORD sign = (aptlval < 0 ? 1 : 0) << 6;
    UWORD bltcon1val = texture | sign | (code << 2) | (p->single << 1) | 0x01;

    APTR start_address = background->imgdata +
        p->plane * (background->header.width / 8 * background->header.height) +
        p->y1 * bytes_per_line + p->x1 / 8;

    WaitBlit();
    custom.bltapt = (APTR) ((UWORD) aptlval);
    custom.bltcpt = start_address;

    // this is actually only used for the first pixel of the line
    // if we point this to another memory area, the first pixel will
    // not be plotted. Research this more and then see whether we should
    // use that in the tutorial
    custom.bltdpt = p->omit_first_pixel ? scratchmem : start_address;

    custom.bltamod = 4 * (dmin - dmax);
    custom.bltbmod = 4 * dmin;

    custom.bltcmod = background->header.width / 8;  // destination width in bytes
    custom.bltdmod = background->header.width / 8;
    custom.bltcon0 = 0x0b00 | p->lf_byte | startx;
    custom.bltcon1 = bltcon1val;

    custom.bltadat = 0x8000;  // draw "pen" pixel
    custom.bltbdat = p->line_pattern;
    custom.bltafwm = 0xffff;
    custom.bltalwm = 0xffff;

    custom.bltsize = ((dmax + 1) << 6) + 2;
}

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

static void draw_triangle(struct Ratr0TileSheet *background, struct TriangleParams *params)
{
    struct DrawLineParams line_params = { 0, 0, 0, 0, 0, 0xffff, 0, LF_XOR, TRUE, TRUE };
    struct AreaFillParams fill_params = { 0, 0, 0, 0, FALSE, 0 };

    line_params.x1 = params->x1;
    line_params.y1 = params->y1;
    line_params.x2 = params->x2;
    line_params.y2 = params->y2;
    draw_line(background, &line_params);

    line_params.x2 = params->x3;
    line_params.y2 = params->y3;
    draw_line(background, &line_params);

    line_params.x1 = params->x2;
    line_params.y1 = params->y2;
    draw_line(background, &line_params);

    if (params->filled) {
        // find bounding box
        UWORD minx = params->x1;
        UWORD maxx = params->x1;
        UWORD miny = params->y1;
        UWORD maxy = params->y1;
        if (params->x2 < minx) minx = params->x2;
        if (params->x3 < minx) minx = params->x3;
        if (params->x2 > maxx) maxx = params->x2;
        if (params->x3 > maxx) maxx = params->x3;

        if (params->y2 < miny) miny = params->y2;
        if (params->y3 < miny) miny = params->y3;
        if (params->y2 > maxy) maxy = params->y2;
        if (params->y3 > maxy) maxy = params->y3;

        fill_params.x1 = minx;
        // we can't ensure an even number of pixels at the top
        fill_params.y1 = miny + 1;
        fill_params.x2 = maxx;
        // we can't ensure an even number of pixels at the bottom
        fill_params.y2 = maxy - 1;
        area_fill(background, &fill_params);
    }
}

/*
 Blit a 64x64 pixel sized region using cookie cut LF byte.
 The source is also the mask.
*/
void blit_rect64x64_cc(char *src, char *dest, int bytes_per_row)
{
    WaitBlit();
    // enable channels A, B, C, D, LF => D = AB + ~AC
    custom.bltcon0 = 0x0fca;
    custom.bltcon1 = 0;  // copy direction

    custom.bltapt = src;
    custom.bltbpt = src;
    custom.bltcpt = dest;
    custom.bltdpt = dest;

    int mod = bytes_per_row - 8;
    custom.bltamod = mod;
    custom.bltbmod = mod;
    custom.bltcmod = mod;
    custom.bltdmod = mod;

    custom.bltafwm = 0xffff;
    custom.bltalwm = 0xffff;

    custom.bltsize = (UWORD) (64 << 6) | (4 & 0x3f);
}

/*
  Copies a 64x64 tile to another 64x64 tile using cookie cut blit. It also uses
  a color parameter to blit into the designated target bit planes.
  Calls blit_rect64x64_cc.
*/
static void copy_tile(struct Ratr0TileSheet *background, struct CopyTileParams *params)
{
    int bytes_per_row = background->header.width / 8;
    int plane_size = background->header.height * bytes_per_row;
    int src_offset = 64 * bytes_per_row * params->srcy + params->srcx * 8;
    int dst_offset = 64 * bytes_per_row * params->dsty + params->dstx * 8;
    UBYTE *dst = background->imgdata;
    int color = params->color;
    for (int i = 0; i < background->header.bmdepth; i++) {
        if (color & 1) {
            blit_rect64x64_cc(background->imgdata + src_offset, dst + dst_offset, bytes_per_row);
        }
        dst += plane_size;
        color >>= 1;
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
