#pragma once
#ifndef __LEVEL_H__
#define __LEVEL_H__

#define FILE_ID_LEN (8)

struct Ratr0SpriteSheetHeader {
    UBYTE id[FILE_ID_LEN];
    UBYTE version, flags;
    UBYTE reserved1, palette_size;
    UWORD num_sprites;
    ULONG imgdata_size;
    UWORD checksum;
};

#define MAX_SPRITE_PALETTE_SIZE (16)
#define MAX_SPRITES_PER_SHEET (16)
struct Ratr0SpriteSheet {
    struct Ratr0SpriteSheetHeader header;
    UWORD palette[MAX_SPRITE_PALETTE_SIZE];
    UWORD sprite_offsets[MAX_SPRITES_PER_SHEET];
    UBYTE *imgdata;
};

extern ULONG ratr0_read_spritesheet(const char *filename, struct Ratr0SpriteSheet *sheet);
extern void ratr0_free_spritesheet_data(struct Ratr0SpriteSheet *sheet);

#endif /* __LEVEL_H__ */
