/* fenv.h - floating-point environment declarations for JCC */

#ifndef __FENV_H
#define __FENV_H

typedef struct { unsigned long long __opaque[4]; } fenv_t;
typedef unsigned int fexcept_t;

#define FE_INVALID 0x01
#define FE_DIVBYZERO 0x04
#define FE_OVERFLOW 0x08
#define FE_UNDERFLOW 0x10
#define FE_INEXACT 0x20
#define FE_ALL_EXCEPT (FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW | FE_INEXACT)

#define FE_TONEAREST 0
#define FE_DOWNWARD 0x400
#define FE_UPWARD 0x800
#define FE_TOWARDZERO 0xc00

#define FE_DFL_ENV ((const fenv_t *)-1)

extern int feclearexcept(int excepts);
extern int fegetexceptflag(fexcept_t *flagp, int excepts);
extern int feraiseexcept(int excepts);
extern int fesetexceptflag(const fexcept_t *flagp, int excepts);
extern int fetestexcept(int excepts);
extern int fegetround(void);
extern int fesetround(int round);
extern int fegetenv(fenv_t *envp);
extern int feholdexcept(fenv_t *envp);
extern int fesetenv(const fenv_t *envp);
extern int feupdateenv(const fenv_t *envp);

#endif /* __FENV_H */
