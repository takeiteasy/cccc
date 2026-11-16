/* complex.h - C99 complex support for CCCC */

#ifndef __COMPLEX_H
#define __COMPLEX_H

#include "math.h"

/* The functions are declared before the macros below, so (cabs)(z), &cabs
   and #undef cabs reach the real function. */
extern double _Complex cacos(double _Complex);
extern float _Complex cacosf(float _Complex);
extern long double _Complex cacosl(long double _Complex);
extern double _Complex cacosh(double _Complex);
extern float _Complex cacoshf(float _Complex);
extern long double _Complex cacoshl(long double _Complex);
extern double _Complex casin(double _Complex);
extern float _Complex casinf(float _Complex);
extern long double _Complex casinl(long double _Complex);
extern double _Complex casinh(double _Complex);
extern float _Complex casinhf(float _Complex);
extern long double _Complex casinhl(long double _Complex);
extern double _Complex catan(double _Complex);
extern float _Complex catanf(float _Complex);
extern long double _Complex catanl(long double _Complex);
extern double _Complex catanh(double _Complex);
extern float _Complex catanhf(float _Complex);
extern long double _Complex catanhl(long double _Complex);
extern double _Complex ccos(double _Complex);
extern float _Complex ccosf(float _Complex);
extern long double _Complex ccosl(long double _Complex);
extern double _Complex ccosh(double _Complex);
extern float _Complex ccoshf(float _Complex);
extern long double _Complex ccoshl(long double _Complex);
extern double _Complex cexp(double _Complex);
extern float _Complex cexpf(float _Complex);
extern long double _Complex cexpl(long double _Complex);
extern double _Complex clog(double _Complex);
extern float _Complex clogf(float _Complex);
extern long double _Complex clogl(long double _Complex);
extern double _Complex conj(double _Complex);
extern float _Complex conjf(float _Complex);
extern long double _Complex conjl(long double _Complex);
extern double _Complex cproj(double _Complex);
extern float _Complex cprojf(float _Complex);
extern long double _Complex cprojl(long double _Complex);
extern double _Complex csin(double _Complex);
extern float _Complex csinf(float _Complex);
extern long double _Complex csinl(long double _Complex);
extern double _Complex csinh(double _Complex);
extern float _Complex csinhf(float _Complex);
extern long double _Complex csinhl(long double _Complex);
extern double _Complex csqrt(double _Complex);
extern float _Complex csqrtf(float _Complex);
extern long double _Complex csqrtl(long double _Complex);
extern double _Complex ctan(double _Complex);
extern float _Complex ctanf(float _Complex);
extern long double _Complex ctanl(long double _Complex);
extern double _Complex ctanh(double _Complex);
extern float _Complex ctanhf(float _Complex);
extern long double _Complex ctanhl(long double _Complex);
extern double _Complex cpow(double _Complex, double _Complex);
extern float _Complex cpowf(float _Complex, float _Complex);
extern long double _Complex cpowl(long double _Complex, long double _Complex);
extern double cabs(double _Complex);
extern float cabsf(float _Complex);
extern long double cabsl(long double _Complex);
extern double carg(double _Complex);
extern float cargf(float _Complex);
extern long double cargl(long double _Complex);
extern double cimag(double _Complex);
extern float cimagf(float _Complex);
extern long double cimagl(long double _Complex);
extern double creal(double _Complex);
extern float crealf(float _Complex);
extern long double creall(long double _Complex);

#define complex          _Complex

#define __cccc_complex_i __cccc_cmplx(0.0, 1.0)
#define _Complex_I       (__cccc_complex_i)
#define I                _Complex_I

#define CMPLX(x, y)      __cccc_cmplx((double)(x), (double)(y))
#define CMPLXF(x, y)     __cccc_cmplxf((float)(x), (float)(y))
#define CMPLXL(x, y)     __cccc_cmplxl((long double)(x), (long double)(y))

#define creal(z)         __cccc_creal(z)
#define crealf(z)        __cccc_crealf(z)
#define creall(z)        __cccc_creall(z)

#define cimag(z)         __cccc_cimag(z)
#define cimagf(z)        __cccc_cimagf(z)
#define cimagl(z)        __cccc_cimagl(z)

#define conj(z)          __cccc_conj(z)
#define conjf(z)         __cccc_conjf(z)
#define conjl(z)         __cccc_conjl(z)

#define cabs(z)          hypot(creal(z), cimag(z))
#define cabsf(z)         hypotf(crealf(z), cimagf(z))
#define cabsl(z)         hypotl(creall(z), cimagl(z))

#define carg(z)          atan2(cimag(z), creal(z))
#define cargf(z)         atan2f(cimagf(z), crealf(z))
#define cargl(z)         atan2l(cimagl(z), creall(z))

#endif /* __COMPLEX_H */
