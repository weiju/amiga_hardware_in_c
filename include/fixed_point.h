#ifndef __FIXED_POINT_H__
#define __FIXED_POINT_H__

#ifdef __VBCC__
#include <exec/types.h>
#else
#include <stdint.h>
typedef int32_t LONG;
#endif

typedef LONG FIXED;

/*
 * 32 bit fixed point math, using 24.8 representation. For now,
 * only addition/subtraction is tested
 */
#define FIXED_SHIFT (8)
#define FIXED_MASK (0xff)

/*
  The amount before the decimal point (integral part and fractional parts)
  are obtained with the macros below. Note that negative numbers need
  to be converted to their absolute equivalent before using this
*/
#define FIXED_INT_ABS(f) (f >> FIXED_SHIFT)
#define FIXED_FRAC_ABS(f) ((f & (FIXED_MASK)) * 100 / FIXED_MASK)
#define FIXED_INT(f) (f < 0 ? -FIXED_INT_ABS((~f + 1)) : FIXED_INT_ABS(f))
#define FIXED_FRAC(f) (f < 0 ? FIXED_FRAC_ABS((~f + 1)) : FIXED_FRAC_ABS(f))
#define FIXED_CREATE_ABS(i, f) ((i << FIXED_SHIFT) | (((f * 256) / 100) & FIXED_MASK))
#define FIXED_CREATE(i, f) (i < 0 ? 0xffffffff - FIXED_CREATE_ABS(-i, f) : FIXED_CREATE_ABS(i, f))
#define FIXED_MUL(f1, f2) (((LONG) (f1 * f2)) >> FIXED_SHIFT)

#endif /* __FIXED_POINT_H__ */
