// -c=native accessor shims (#904/#1021/#1052/#1069/#1139):
// stdin/stdout/errno/optarg, isnan/isinf/signbit/fpclassify, FLT_ROUNDS,
// issignaling/iseqsig, MB_CUR_MAX, environ. Reached through a header macro
// that expands to a call to the shim of the same name.
//
// Source of truth for the text tools/gen_shims.py embeds into
// src/shims.inc. NOT COMPILED. Gating and rationale live in the
// matching serialize_*_shims() in src/serialize_shims.c.

// >>> shim: __cccc_stdin
static FILE *__cccc_stdin(void) { return stdin; }
// <<< shim

// >>> shim: __cccc_stdout
static FILE *__cccc_stdout(void) { return stdout; }
// <<< shim

// >>> shim: __cccc_stderr
static FILE *__cccc_stderr(void) { return stderr; }
// <<< shim

// >>> shim: __cccc_errno_ptr
static int *__cccc_errno_ptr(void) { return &errno; }
// <<< shim

// >>> shim: __cccc_optarg_ptr
static char **__cccc_optarg_ptr(void) { return &optarg; }
// <<< shim

// >>> shim: __cccc_optind_ptr
static int *__cccc_optind_ptr(void) { return &optind; }
// <<< shim

// >>> shim: __cccc_opterr_ptr
static int *__cccc_opterr_ptr(void) { return &opterr; }
// <<< shim

// >>> shim: __cccc_optopt_ptr
static int *__cccc_optopt_ptr(void) { return &optopt; }
// <<< shim

// >>> shim: __cccc_isnan_f
static int __cccc_isnan_f(float x) { return __builtin_isnan(x); }
// <<< shim

// >>> shim: __cccc_isnan_d
static int __cccc_isnan_d(double x) { return __builtin_isnan(x); }
// <<< shim

// >>> shim: __cccc_isinf_f
static int __cccc_isinf_f(float x) { return __builtin_isinf(x); }
// <<< shim

// >>> shim: __cccc_isinf_d
static int __cccc_isinf_d(double x) { return __builtin_isinf(x); }
// <<< shim

// >>> shim: __cccc_signbit_f
static int __cccc_signbit_f(float x) { return __builtin_signbit(x); }
// <<< shim

// >>> shim: __cccc_signbit_d
static int __cccc_signbit_d(double x) { return __builtin_signbit(x); }
// <<< shim

// >>> shim: __cccc_fpclassify_f
static int __cccc_fpclassify_f(float x) { return __builtin_fpclassify(2, 1, 3, 4, 5, x); }
// <<< shim

// >>> shim: __cccc_fpclassify_d
static int __cccc_fpclassify_d(double x) { return __builtin_fpclassify(2, 1, 3, 4, 5, x); }
// <<< shim

// >>> shim: __cccc_flt_rounds
#include <fenv.h>
static int __cccc_flt_rounds(void) {
    switch (fegetround()) {
    case FE_TOWARDZERO: return 0;
    case FE_TONEAREST:  return 1;
    case FE_UPWARD:     return 2;
    case FE_DOWNWARD:   return 3;
    default:            return -1;
    }
}
// <<< shim

// >>> shim: __cccc_issignaling_f
static int __cccc_issignaling_f(float x) {
    union { float f; unsigned int u; } __v; __v.f = x;
    unsigned int u = __v.u;
    return ((u & 0x7F800000U) == 0x7F800000U) && (u & 0x003FFFFFU) != 0 && !(u & 0x00400000U);
}
// <<< shim

// >>> shim: __cccc_issignaling_d
static int __cccc_issignaling_d(double x) {
    union { double d; unsigned long long u; } __v; __v.d = x;
    unsigned long long u = __v.u;
    return ((u & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) && (u & 0x0007FFFFFFFFFFFFULL) != 0 && !(u & 0x0008000000000000ULL);
}
// <<< shim

// >>> shim: __cccc_iseqsig_f
#include <fenv.h>
static int __cccc_iseqsig_f(float x, float y) {
    union { float f; unsigned int u; } __vx, __vy; __vx.f = x; __vy.f = y;
    unsigned int ux = __vx.u, uy = __vy.u;
    int sx = ((ux & 0x7F800000U) == 0x7F800000U) && (ux & 0x003FFFFFU) != 0 && !(ux & 0x00400000U);
    int sy = ((uy & 0x7F800000U) == 0x7F800000U) && (uy & 0x003FFFFFU) != 0 && !(uy & 0x00400000U);
    if (sx || sy) feraiseexcept(FE_INVALID);
    return x == y;
}
// <<< shim

// >>> shim: __cccc_iseqsig_d
#include <fenv.h>
static int __cccc_iseqsig_d(double x, double y) {
    union { double d; unsigned long long u; } __vx, __vy; __vx.d = x; __vy.d = y;
    unsigned long long ux = __vx.u, uy = __vy.u;
    int sx = ((ux & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) && (ux & 0x0007FFFFFFFFFFFFULL) != 0 && !(ux & 0x0008000000000000ULL);
    int sy = ((uy & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) && (uy & 0x0007FFFFFFFFFFFFULL) != 0 && !(uy & 0x0008000000000000ULL);
    if (sx || sy) feraiseexcept(FE_INVALID);
    return x == y;
}
// <<< shim

// >>> shim: __cccc_mb_cur_max__linux
extern size_t __ctype_get_mb_cur_max(void);
static size_t __cccc_mb_cur_max(void) { return __ctype_get_mb_cur_max(); }
// <<< shim

// >>> shim: __cccc_mb_cur_max__other
extern int __mb_cur_max;
static size_t __cccc_mb_cur_max(void) { return (size_t)__mb_cur_max; }
// <<< shim

// >>> shim: __cccc_environ_ptr
#undef environ
extern char **environ;
static char ***__cccc_environ_ptr(void) { return &environ; }
// <<< shim
