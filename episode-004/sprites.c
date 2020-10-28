#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <clib/exec_protos.h>
#include <clib/graphics_protos.h>
#include <hardware/custom.h>
extern struct Custom custom;

#include "sprites.h"

ULONG ratr0_read_spritesheet(const char *filename, struct Ratr0SpriteSheet *sheet)
{
    int elems_read;
    ULONG retval = 0;
    FILE *fp = fopen(filename, "rb");

    if (fp) {
        elems_read = fread(&sheet->header, sizeof(struct Ratr0SpriteSheetHeader), 1, fp);
        elems_read = fread(&sheet->sprite_offsets, sizeof(UWORD), sheet->header.num_sprites, fp);
        elems_read = fread(&sheet->palette, sizeof(UWORD), sheet->header.palette_size, fp);
        sheet->imgdata = AllocMem(sheet->header.imgdata_size, MEMF_CHIP|MEMF_CLEAR);
        elems_read = fread(sheet->imgdata, sizeof(UBYTE), sheet->header.imgdata_size, fp);
        fclose(fp);
        retval = elems_read;
    } else {
        printf("ratr0_read_spritesheet() error: file '%s' not found\n", filename);
    }
    return retval;
}

void ratr0_free_spritesheet_data(struct Ratr0SpriteSheet *sheet)
{
    if (sheet && sheet->imgdata) FreeMem(sheet->imgdata, sheet->header.imgdata_size);
}
