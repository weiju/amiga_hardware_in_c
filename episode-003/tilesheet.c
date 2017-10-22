#include <stdio.h>
#include <clib/exec_protos.h>
#include "tilesheet.h"


ULONG read_tilesheet(const char *filename, struct Ratr0TileSheet *sheet,
                     int min_imagedata_size)
{
    int elems_read;
    FILE *fp = fopen(filename, "rb");

    if (fp) {
        int num_img_bytes, imgdata_size = min_imagedata_size, total_bytes = 0;
        elems_read = fread(&sheet->header, sizeof(struct Ratr0TileSheetHeader), 1, fp);
        total_bytes += elems_read * sizeof(struct Ratr0TileSheetHeader);
        elems_read = fread(&sheet->palette, sizeof(UWORD), sheet->header.palette_size, fp);
        total_bytes += elems_read * sizeof(UWORD);
        // reserve enough data to fill the entire display window
        // if we have only an NTSC sized image, but a PAL display, we might get
        // artifacts
        if (sheet->header.imgdata_size > imgdata_size) {
            imgdata_size = sheet->header.imgdata_size;
        }
        sheet->imgdata = AllocMem(imgdata_size, MEMF_CHIP|MEMF_CLEAR);
        elems_read = fread(sheet->imgdata, sizeof(unsigned char), sheet->header.imgdata_size, fp);
        total_bytes += elems_read;
        fclose(fp);
    } else {
        printf("ratr0_read_tilesheet() error: file '%s' not found\n", filename);
        return 0;
    }
    return elems_read;
}

void free_tilesheet_data(struct Ratr0TileSheet *sheet, int imgdata_size)
{
    // Since the AllocMem()/FreeMem() pair needs an explicit specification
    // of the allocated/freed memory block size, we need to pass that size
    // as well to avoid a memory leak in the case that the image size is
    // smaller than the display size (e.g. on PAL)
    if (sheet && sheet->imgdata) FreeMem(sheet->imgdata, imgdata_size);
}
