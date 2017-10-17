#include <stdio.h>
#include <hardware/custom.h>
#include <clib/exec_protos.h>
#include <clib/intuition_protos.h>
#include <clib/graphics_protos.h>
#include <graphics/gfxbase.h>
#include <ahpc_registers.h>

// Data fetch
#define DDFSTRT_VALUE      0x0038
#define DDFSTOP_VALUE      0x00d0
#define DIWSTRT_VALUE      0x2c81
#define DIWSTOP_VALUE_PAL  0x2cc1
#define DIWSTOP_VALUE_NTSC 0xf4c1

#define DISPLAY_WIDTH    (320)
#define DISPLAY_HEIGHT   (200)
#define SCREEN_WIDTH     (320)
#define SCREEN_ROW_BYTES (SCREEN_WIDTH / 8)
#define NUM_BITPLANES    (5)
#define BPL_MODULO ((NUM_BITPLANES - 1) * SCREEN_ROW_BYTES)

#define PAL_IMAGE_SIZE   (SCREEN_ROW_BYTES * 256)

// playfield control
// bplcon0: use bitplane 1-5 = BPU 101, composite color enable
// bplcon1: horizontal scroll value = 0 for all playfields
#define BPLCON0_VALUE (0x5200)
#define BPLCON1_VALUE (0)

// information about a tile sheet
#define FILE_ID_LEN (8)

struct Ratr0TileSheetHeader {
    UBYTE id[FILE_ID_LEN];
    UBYTE flags, bmdepth;
    UWORD width, height;
    UWORD tile_width, tile_height;
    UWORD num_tiles_h, num_tiles_v;
    ULONG checksum;
    UWORD palette_size;
    ULONG imgdata_size;
};

# define MAX_PALETTE_SIZE (32 * 3)
struct Ratr0TileSheet {
    struct Ratr0TileSheetHeader header;
    UBYTE palette[MAX_PALETTE_SIZE];
    UBYTE *imgdata;
};

#define IMG_FILE_NAME "gorilla.tiles"

// 20 instead of 127 because of input.device priority
#define TASK_PRIORITY           (20)
#define PRA_FIR0_BIT            (1 << 6)

// copper instruction macros
#define COP_MOVE(addr, data) addr, data
#define COP_WAIT_END  0xffff, 0xfffe

// copper list indexes
#define COPLIST_IDX_DIWSTOP_VALUE (9)
#define COPLIST_IDX_COLOR00_VALUE (19)
#define COPLIST_IDX_BPL1PTH_VALUE (19 + 64)

extern struct GfxBase *GfxBase;
extern struct Custom custom;

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

    // set up the screen colors
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
    COP_WAIT_END
};

static ULONG read_tilesheet(const char *filename, struct Ratr0TileSheet *sheet)
{
    int elems_read;
    FILE *fp = fopen(filename, "rb");

    if (fp) {
        int num_img_bytes, imgdata_size = PAL_IMAGE_SIZE;
        elems_read = fread(&sheet->header, sizeof(struct Ratr0TileSheetHeader), 1, fp);
        elems_read = fread(&sheet->palette, sizeof(unsigned char), 3 * sheet->header.palette_size, fp);
        // reserve enough data to fill the entire display window
        // if we have only an NTSC sized image, but a PAL display, we might get
        // artifacts
        if (sheet->header.imgdata_size > imgdata_size) {
            imgdata_size = sheet->header.imgdata_size;
        }
        sheet->imgdata = AllocMem(imgdata_size, MEMF_CHIP|MEMF_CLEAR);
        elems_read = fread(sheet->imgdata, sizeof(unsigned char), sheet->header.imgdata_size, fp);
        fclose(fp);
    } else {
        printf("ratr0_read_tilesheet() error: file '%s' not found\n", filename);
        return 0;
    }
    return elems_read;
}

static void free_tilesheet_data(struct Ratr0TileSheet *sheet)
{
    if (sheet && sheet->imgdata) FreeMem(sheet->imgdata, sheet->header.imgdata_size);
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
    if (read_tilesheet(IMG_FILE_NAME, &image)) {
        if (is_pal) {
            coplist[COPLIST_IDX_DIWSTOP_VALUE] = DIWSTOP_VALUE_PAL;
        } else {
            coplist[COPLIST_IDX_DIWSTOP_VALUE] = DIWSTOP_VALUE_NTSC;
        }
        UBYTE num_colors = 1 << image.header.bmdepth;

        // 1. copy the palette to the copper list
        // TODO: maybe we should already encode the color entries
        // in 12 bit format through the tool
        UWORD color_triple = 0;
        for (int i = 0, j = 0; i < num_colors; i++, j += 3) {
            UWORD color_triple = (((image.palette[j] >> 4) & 0x0f) << 8) |
                (((image.palette[j + 1] >> 4) & 0x0f) << 4) |
                ((image.palette[j + 2] >> 4) & 0x0f);
            coplist[COPLIST_IDX_COLOR00_VALUE + (i << 1)] = color_triple;
        }
        // 2. prepare bitplanes and point the copper list entries
        // to the bitplanes (we already initialized the modulos statically)
        int coplist_idx = COPLIST_IDX_BPL1PTH_VALUE;
        for (int i = 0; i < image.header.bmdepth; i++) {
            ULONG addr = (ULONG) &(image.imgdata[i * SCREEN_ROW_BYTES]);
            coplist[coplist_idx] = (addr >> 16) & 0xffff;
            coplist[coplist_idx + 2] = addr & 0xffff;
            coplist_idx += 4; // next bitplane
        }

        // 3. initialize the copper list
        custom.cop1lc = (ULONG) coplist;
        waitmouse();
        free_tilesheet_data(&image);
    }
    reset_display();
    return 0;
}
