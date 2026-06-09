/* complex.h - C99 complex support for CCCC */

#ifndef __COMPLEX_H
#define __COMPLEX_H

#include "math.h"

#define complex _Complex

#define __cccc_complex_i __cccc_cmplx(0.0, 1.0)
#define _Complex_I (__cccc_complex_i)
#define I _Complex_I

#define CMPLX(x, y) __cccc_cmplx((double)(x), (double)(y))
#define CMPLXF(x, y) __cccc_cmplxf((float)(x), (float)(y))
#define CMPLXL(x, y) __cccc_cmplxl((long double)(x), (long double)(y))

#define creal(z) __cccc_creal(z)
#define crealf(z) __cccc_crealf(z)
#define creall(z) __cccc_creall(z)

#define cimag(z) __cccc_cimag(z)
#define cimagf(z) __cccc_cimagf(z)
#define cimagl(z) __cccc_cimagl(z)

#define conj(z) __cccc_conj(z)
#define conjf(z) __cccc_conjf(z)
#define conjl(z) __cccc_conjl(z)

#define cabs(z) hypot(creal(z), cimag(z))
#define cabsf(z) hypotf(crealf(z), cimagf(z))
#define cabsl(z) hypotl(creall(z), cimagl(z))

#define carg(z) atan2(cimag(z), creal(z))
#define cargf(z) atan2f(cimagf(z), crealf(z))
#define cargl(z) atan2l(cimagl(z), creall(z))

#endif /* __COMPLEX_H */
