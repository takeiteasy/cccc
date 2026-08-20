/* fenv.h - floating-point environment declarations for CCCC */

#ifndef __FENV_H
#define __FENV_H

/* #1021: this exact file is also what a native/generated re-emission's
 * replayed `#include <fenv.h>` resolves to (run_native_backend forwards
 * -I./include straight through to the host cc, and -I paths are searched
 * ahead of system directories) -- but the content below needs
 * __CCCC_SIZEOF_FENV_T__/__CCCC_FE_*, macros only CCCC's OWN preprocessor
 * (init_fenv_macros(), src/preprocess.c) ever injects. A real host compiler
 * reprocessing this text from scratch has never heard of them and fails
 * outright ("use of undeclared identifier '__CCCC_SIZEOF_FENV_T__'").
 * __CCCC__ is defined unconditionally by CCCC's own preprocessor
 * (init_macros(), src/preprocess.c) before any header is ever read, so its
 * absence here means this exact physical file is being read by a genuine
 * host compiler instead -- during -c=native/-c=generated serializer
 * replay, which is the only way a non-CCCC compiler ever sees this path at
 * all. In that case, hand off to #include_next: found via -I (searched
 * before the system dirs), this file's own #include_next continues the
 * search *after* that directory and lands on the host's real,
 * self-contained <fenv.h> -- exactly the header CCCC's own guest-side
 * parser has no access to and this file exists to stand in for.
 * Spelled `#ifdef __CCCC__` (not `#if !defined(__CCCC__) ... #else`) so
 * tools/audit_ffi.py's guard-presence check sees a plain, nameable
 * condition it can whitelist (GUEST_ONLY_DECL_GUARDS) the same way it
 * already does for __STDC_IEC_60559_DFP__ -- src/stdlib/fenv.c's
 * registrations are unconditional, so a `!(!defined(__CCCC__))`-shaped
 * guard here would misreport as a mismatch. */
#ifdef __CCCC__

/* fexcept_t/fenv_t are sized from the real host <fenv.h> that
 * src/stdlib/fenv.c's wrap_fe*() functions were compiled against
 * (__CCCC_SIZEOF_FEXCEPT_T__/__CCCC_SIZEOF_FENV_T__, injected by
 * init_fenv_macros() in src/preprocess.c). This matters: on macOS/arm64,
 * fexcept_t is 2 bytes, not the 4-byte `unsigned int` this header used to
 * assume, so fegetexceptflag() left half of the guest's output object
 * uninitialized. fenv_t is opaque either way -- guest code never inspects
 * its fields, only round-trips it through fegetenv()/fesetenv() -- so it is
 * sized generously (rounded up to whole 8-byte words) rather than exactly.
 */
#if __CCCC_SIZEOF_FEXCEPT_T__ == 2
typedef unsigned short fexcept_t;
#elif __CCCC_SIZEOF_FEXCEPT_T__ == 4
typedef unsigned int fexcept_t;
#else
typedef unsigned long fexcept_t;
#endif

typedef struct {
    unsigned long long __opaque[(__CCCC_SIZEOF_FENV_T__ + 7) / 8];
} fenv_t;

/* FE_* values are the real host <fenv.h> values for the machine this
 * compiler was built on -- NOT hardcoded, and NOT an OR of the individual
 * exception macros for FE_ALL_EXCEPT (some platforms have additional bits,
 * e.g. x86's FE_DENORMAL, with no portable C name). See #771. */
#define FE_INVALID    __CCCC_FE_INVALID__
#define FE_DIVBYZERO  __CCCC_FE_DIVBYZERO__
#define FE_OVERFLOW   __CCCC_FE_OVERFLOW__
#define FE_UNDERFLOW  __CCCC_FE_UNDERFLOW__
#define FE_INEXACT    __CCCC_FE_INEXACT__
#define FE_ALL_EXCEPT __CCCC_FE_ALL_EXCEPT__

#define FE_TONEAREST  __CCCC_FE_TONEAREST__
#define FE_DOWNWARD   __CCCC_FE_DOWNWARD__
#define FE_UPWARD     __CCCC_FE_UPWARD__
#define FE_TOWARDZERO __CCCC_FE_TOWARDZERO__

/* Sentinel recognized by fesetenv()/feupdateenv()/fegetenv() in
 * src/stdlib/fenv.c and substituted for the real host FE_DFL_ENV (which is
 * an opaque host address on some platforms, e.g. &_FE_DFL_ENV on macOS --
 * not representable as a guest-side constant). */
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

#else
#include_next <fenv.h>
#endif /* __CCCC__ */

#endif /* __FENV_H */
