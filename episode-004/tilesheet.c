#include <stdio.h>
#include <clib/exec_protos.h>
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
    FILE *fp = fopen(filename, "rb");

    if (fp) {
        int num_img_bytes, total_bytes = 0;
        elems_read = fread(&sheet->header, sizeof(struct Ratr0TileSheetHeader), 1, fp);
        total_bytes += elems_read * sizeof(struct Ratr0TileSheetHeader);
        elems_read = fread(&sheet->palette, sizeof(UWORD), sheet->header.palette_size, fp);
        total_bytes += elems_read * sizeof(UWORD);
        sheet->imgdata = AllocMem(sheet->header.imgdata_size, MEMF_CHIP|MEMF_CLEAR);
        elems_read = fread(sheet->imgdata, sizeof(unsigned char), sheet->header.imgdata_size, fp);
        total_bytes += elems_read;
        fclose(fp);
    } else {
        printf("ratr0_read_tilesheet() error: file '%s' not found\n", filename);
        return 0;
    }
    return elems_read;
}

/**
 * Frees the memory that was allocated for the specified RATR0 tile sheet.
 */
void ratr0_free_tilesheet_data(struct Ratr0TileSheet *sheet)
{
    if (sheet && sheet->imgdata) FreeMem(sheet->imgdata, sheet->header.imgdata_size);
}
