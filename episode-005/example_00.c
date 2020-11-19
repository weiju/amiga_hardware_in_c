/**
 * example_00.c - string copy exmaple
 * This is a program to demonstrate basic character copy using
 * the blitter.
 */
#include <stdio.h>

#include <clib/graphics_protos.h>

#include <hardware/custom.h>
extern struct Custom custom;

#define ASC  (0)
#define DESC (1)

/*
 * A *very* basic copy function that can only copy up to 63 bytes.
 * The number of bytes must be a multiple of 2, because our
 * logic function will be D := A and we will not even use the mask
 * That's because for simplicity, we will only assume BLTSIZE's height to be 1.
 */
void copy_mem(char *src, char *dest, int num_words, int desc)
{
    WaitBlit();
    custom.bltcon0 = 0x09f0;       // enable channels A and D, LF => D = A
    custom.bltcon1 = desc ? 2: 0;  // copy direction

    custom.bltapt = src;
    custom.bltdpt = dest;

    // modulo = 0, we just assume we copy a linear array
    custom.bltamod = 0;
    custom.bltdmod = 0;

    // copy everything, bltafwm and bltalwm are all set to 1's
    custom.bltafwm = 0xffff;
    custom.bltalwm = 0xffff;

    // B and C are disabled, just set their data registers to all 1's
    custom.bltbdat = 0xffff;
    custom.bltcdat = 0xffff;

    custom.bltsize = (UWORD) (1 << 6) | (num_words & 0x3f);
}

static __chip char src1[] = "ABCDEFGHIJK";
static __chip char dst1[] = "01234567890";

static __chip char ovl_s1[] = "ABCDEFGHIJ00001";
static __chip char ovl_s2[] = "ABCDEFGHIJ00001";

static __chip char ovl_s3[] = "0000ABCDEFGHIJZ";
static __chip char ovl_s4[] = "0000ABCDEFGHIJZ";

int main(int argc, char **argv)
{
    OwnBlitter();

    // no overlap
    copy_mem(src1, dst1, 3, ASC);

    // overlap, dst > src
    copy_mem(ovl_s1, ovl_s1 + 4, 5, ASC);
    copy_mem(ovl_s2 + 8, ovl_s2 + 12, 5, DESC);

    // overlap, dst < src
    copy_mem(ovl_s3 + 4, ovl_s3, 5, ASC);
    copy_mem(ovl_s4 + 12, ovl_s4 + 8, 5, DESC);

    DisownBlitter();
    printf("non-overlapped copy: '%s'\n", dst1);
    printf("overlap (dst > src), ascending copy: '%s'\n", ovl_s1);
    printf("overlap (dst > src), descending copy: '%s'\n", ovl_s2);

    printf("overlap (dst < src), ascending copy: '%s'\n", ovl_s3);
    printf("overlap (dst < src), descending copy: '%s'\n", ovl_s4);
    return 0;
}
