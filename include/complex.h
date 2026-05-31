/* complex.h - C99 complex support for JCC */

#ifndef __COMPLEX_H
#define __COMPLEX_H

#include "math.h"

#define complex _Complex

#define __jcc_complex_i __jcc_cmplx(0.0, 1.0)
#define _Complex_I (__jcc_complex_i)
#define I _Complex_I

#define CMPLX(x, y) __jcc_cmplx((double)(x), (double)(y))
#define CMPLXF(x, y) __jcc_cmplxf((float)(x), (float)(y))
#define CMPLXL(x, y) __jcc_cmplxl((long double)(x), (long double)(y))

#define creal(z) __jcc_creal(z)
#define crealf(z) __jcc_crealf(z)
#define creall(z) __jcc_creall(z)

#define cimag(z) __jcc_cimag(z)
#define cimagf(z) __jcc_cimagf(z)
#define cimagl(z) __jcc_cimagl(z)

#define conj(z) __jcc_conj(z)
#define conjf(z) __jcc_conjf(z)
#define conjl(z) __jcc_conjl(z)

#define cabs(z) hypot(creal(z), cimag(z))
#define cabsf(z) hypotf(crealf(z), cimagf(z))
#define cabsl(z) hypotl(creall(z), cimagl(z))

#define carg(z) atan2(cimag(z), creal(z))
#define cargf(z) atan2f(cimagf(z), crealf(z))
#define cargl(z) atan2l(cimagl(z), creall(z))

#endif /* __COMPLEX_H */
