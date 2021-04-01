#include <stdio.h>
#include <stdlib.h>
#include <hardware/custom.h>
#include <clib/exec_protos.h>
#include <clib/graphics_protos.h>
#include "tilesheet.h"

/**
 * Reads the image information from specified RATR0 tile sheet file.
 *
 * @param filename path to the tile sheet file
 * @param sheet pointer to a Ratr0TileSheet structure
 */
ULONG ratr0_read_tilesheet(const char *filename, struct Ratr0TileSheet *sheet)
{
    int elems_read;
    ULONG retval = 0;

    FILE *fp = fopen(filename, "rb");

    if (fp) {
        int num_img_bytes;
        elems_read = fread(&sheet->header, sizeof(struct Ratr0TileSheetHeader), 1, fp);
        elems_read = fread(&sheet->palette, sizeof(UWORD), sheet->header.palette_size, fp);
        sheet->imgdata = AllocMem(sheet->header.imgdata_size, MEMF_CHIP|MEMF_CLEAR);
        elems_read = fread(sheet->imgdata, sizeof(unsigned char), sheet->header.imgdata_size, fp);
        fclose(fp);
        return 1;
    } else {
        printf("ratr0_read_tilesheet() error: file '%s' not found\n", filename);
        return 0;
    }
}

/**
 * Frees the memory that was allocated for the specified RATR0 tile sheet.
 */
void ratr0_free_tilesheet_data(struct Ratr0TileSheet *sheet)
{
    if (sheet && sheet->imgdata) FreeMem(sheet->imgdata, sheet->header.imgdata_size);
}

/**
 * Reads the data from the specified RATR0 level file.
 *
 * @param filename path to the level file
 * @param level pointer to a Ratr0Level structure
 */
BOOL ratr0_read_level(const char *filename, struct Ratr0Level *level)
{
    int elems_read;
    BOOL retval;

    FILE *fp = fopen(filename, "rb");

    if (fp) {
        int num_img_bytes, data_size;
        elems_read = fread(&level->header, sizeof(struct Ratr0LevelHeader), 1, fp);
        data_size = sizeof(UBYTE) * level->header.width * level->header.height;
        level->lvldata = malloc(data_size);
        elems_read = fread(level->lvldata, sizeof(unsigned char), data_size, fp);
        fclose(fp);
        return TRUE;
    } else {
        printf("ratr0_read_level() error: file '%s' not found\n", filename);
        return FALSE;
    }
}

/**
 * Frees the memory that was allocated for the specified RATR0 tile sheet.
 */
void ratr0_free_level_data(struct Ratr0Level *level)
{
    if (level && level->lvldata) free(level->lvldata);
}


/**
 * tx, ty are tileset coordinates
 */
extern struct Custom custom;
void ratr0_blit_tile(UBYTE *dst, int dmod, struct Ratr0TileSheet *tileset, int tx, int ty)
{
    int height = tileset->header.tile_height * tileset->header.bmdepth;
    int num_words = 1;

    // map tilenum to offset
    int tile_row_bytes = tileset->header.num_tiles_h * 2 * tileset->header.tile_width * tileset->header.bmdepth;
    int src_offset = ty * tile_row_bytes + tx * 2;

    WaitBlit();
    custom.bltcon0 = 0x9f0;       // enable channels A and D, LF => D = A
    custom.bltcon1 = 0;            // copy direction: asc
    custom.bltapt = tileset->imgdata + src_offset;
    custom.bltdpt = dst;
    custom.bltamod = (tileset->header.num_tiles_h - 1) * 2;
    custom.bltdmod = dmod;
    // copy everything, bltafwm and bltalwm are all set to 1's
    custom.bltafwm = 0xffff;
    custom.bltalwm = 0xffff;

    // B and C are disabled, just set their data registers to all 1's
    custom.bltbdat = 0xffff;
    custom.bltcdat = 0xffff;

    custom.bltsize = (UWORD) (height << 6) | (num_words & 0x3f);
}
