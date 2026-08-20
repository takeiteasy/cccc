/* float.h - floating point characteristics for CCCC C compiler */

#ifndef __FLOAT_H
#define __FLOAT_H

#define FLT_RADIX       2

#define FLT_MANT_DIG    24
#define DBL_MANT_DIG    53
#define LDBL_MANT_DIG   53

#define FLT_DIG         6
#define DBL_DIG         15
#define LDBL_DIG        15

#define FLT_MIN_EXP     (-125)
#define DBL_MIN_EXP     (-1021)
#define LDBL_MIN_EXP    (-1021)

#define FLT_MAX_EXP     128
#define DBL_MAX_EXP     1024
#define LDBL_MAX_EXP    1024

#define FLT_MAX_10_EXP  38
#define DBL_MAX_10_EXP  308
#define LDBL_MAX_10_EXP 308

#define FLT_MIN_10_EXP  (-37)
#define DBL_MIN_10_EXP  (-307)
#define LDBL_MIN_10_EXP (-307)

#define FLT_MAX         3.40282347e+38F
#define DBL_MAX         1.7976931348623157e+308
#define LDBL_MAX        1.7976931348623157e+308

#define FLT_MIN         1.17549435e-38F
#define DBL_MIN         2.2250738585072014e-308
#define LDBL_MIN        2.2250738585072014e-308

#define FLT_EPSILON     1.19209290e-07F
#define DBL_EPSILON     2.2204460492503131e-16
#define LDBL_EPSILON    2.2204460492503131e-16

/* C11 additions */
#define DECIMAL_DIG      17
#define FLT_EVAL_METHOD  0

#define FLT_HAS_SUBNORM  1
#define DBL_HAS_SUBNORM  1
#define LDBL_HAS_SUBNORM 1

#define FLT_TRUE_MIN     1.40129846e-45F
#define DBL_TRUE_MIN     4.9406564584124654e-324
#define LDBL_TRUE_MIN    4.9406564584124654e-324

#define FLT_DECIMAL_DIG  9
#define DBL_DECIMAL_DIG  17
#define LDBL_DECIMAL_DIG 17

/* C23 additions */
#define FLT_NORM_MAX  FLT_MAX
#define DBL_NORM_MAX  DBL_MAX
#define LDBL_NORM_MAX LDBL_MAX

#define FLT_SNAN      (__builtin_nansf(""))
#define DBL_SNAN      (__builtin_nans(""))
#define LDBL_SNAN     (__builtin_nansl(""))

/* Rounding mode as last set by fesetround() (<fenv.h>); required by C11 to
 * track the dynamic rounding mode rather than being a static constant.
 * Declared here (not via #include <fenv.h>) so <float.h> stays standalone.
 *
 * #1021: guarded on __CCCC__, same reasoning as include/math.h's matching
 * comment on isnan_f/etc -- an unconditional `extern` here would conflict
 * with the `static` definition serialize.c's native_accessor_shims emits
 * once __cccc_flt_rounds is used, when a real host compiler reprocesses
 * this replayed #include during -c=native/-c=generated. */
#ifdef __CCCC__
extern int __cccc_flt_rounds(void);
#endif
#define FLT_ROUNDS (__cccc_flt_rounds())

/* C23 IEC 60559 decimal floating-point (#402): declarations, sizeof, and
 * struct layout for _Decimal32/64/128 always work; these characteristic
 * macros are only meaningful (and only predefined as available) once the
 * program is built with CCCC_HAS_DECIMAL=1 -- the same condition
 * __STDC_IEC_60559_DFP__ signals. Values are the IEEE 754-2008 / ISO/IEC
 * TS 18661-2 decimal32/64/128 format constants (7/16/34 significant
 * decimal digits; the same values GCC's decfloat.h and glibc use). */
#ifdef __STDC_IEC_60559_DFP__

#define DEC_EVAL_METHOD                                                        \
    0 /* each decimal operation/constant evaluated in                          \
          its own type -- this implementation does not                         \
          promote to a wider decimal type internally */

#define DEC32_MANT_DIG       7
#define DEC64_MANT_DIG       16
#define DEC128_MANT_DIG      34

#define DEC32_MIN_EXP        (-94)
#define DEC64_MIN_EXP        (-382)
#define DEC128_MIN_EXP       (-6142)

#define DEC32_MAX_EXP        97
#define DEC64_MAX_EXP        385
#define DEC128_MAX_EXP       6145

#define DEC32_MAX            9.999999E96DF
#define DEC64_MAX            9.999999999999999E384DD
#define DEC128_MAX           9.999999999999999999999999999999999E6144DL

#define DEC32_MIN            1E-95DF
#define DEC64_MIN            1E-383DD
#define DEC128_MIN           1E-6143DL

#define DEC32_EPSILON        1E-6DF
#define DEC64_EPSILON        1E-15DD
#define DEC128_EPSILON       1E-33DL

#define DEC32_TRUE_MIN       1E-101DF
#define DEC64_TRUE_MIN       1E-398DD
#define DEC128_TRUE_MIN      1E-6176DL

#define DEC32_SUBNORMAL_MIN  DEC32_TRUE_MIN
#define DEC64_SUBNORMAL_MIN  DEC64_TRUE_MIN
#define DEC128_SUBNORMAL_MIN DEC128_TRUE_MIN

#define DEC_INFINITY         (__builtin_infd64())
#define DEC_NAN              (__builtin_nand64(""))

#endif /* __STDC_IEC_60559_DFP__ */

#endif /* __FLOAT_H */
