// complex.h stdlib function registration
//
// A complex argument reaches a native as a pointer to its two parts, and a
// complex result goes back as a pointer to a thread-local slot, as in the
// div() wrappers (stdlib.c). The host's own complex ABI never meets the VM's.
#include "../cccc.h"
#include <complex.h>
#include <stdint.h>
#include <string.h>

// The VM models long double as a double; its complex parts sit one long
// double apart.
#if defined(__APPLE__) && defined(__aarch64__)
#define CCCC_LD_SIZE 8
#else
#define CCCC_LD_SIZE 16
#endif

#define CCCC_CX_SLOTS 16
#define CCCC_CX_BYTES (2 * CCCC_LD_SIZE)

static _Thread_local unsigned char g_cx_pool[CCCC_CX_SLOTS][CCCC_CX_BYTES];
static _Thread_local int           g_cx_idx;

static unsigned char *cx_slot(void) {
    unsigned char *slot = g_cx_pool[g_cx_idx];
    g_cx_idx            = (g_cx_idx + 1) % CCCC_CX_SLOTS;
    return slot;
}

static double _Complex cx_get_d(long long p) {
    double v[2];
    memcpy(v, (const void *)(intptr_t)p, sizeof v);
    double _Complex z;
    memcpy(&z, v, sizeof z);
    return z;
}

static float _Complex cx_get_f(long long p) {
    float v[2];
    memcpy(v, (const void *)(intptr_t)p, sizeof v);
    float _Complex z;
    memcpy(&z, v, sizeof z);
    return z;
}

static double _Complex cx_get_l(long long p) {
    const unsigned char *src = (const void *)(intptr_t)p;
    double               re, im;
    memcpy(&re, src, sizeof re);
    memcpy(&im, src + CCCC_LD_SIZE, sizeof im);
    double v[2] = {re, im};
    double _Complex z;
    memcpy(&z, v, sizeof z);
    return z;
}

static long long cx_put_d(double _Complex z) {
    unsigned char *slot = cx_slot();
    double         v[2] = {creal(z), cimag(z)};
    memcpy(slot, v, sizeof v);
    return (long long)(intptr_t)slot;
}

static long long cx_put_f(float _Complex z) {
    unsigned char *slot = cx_slot();
    float          v[2] = {crealf(z), cimagf(z)};
    memcpy(slot, v, sizeof v);
    return (long long)(intptr_t)slot;
}

static long long cx_put_l(double _Complex z) {
    unsigned char *slot = cx_slot();
    double         re = creal(z), im = cimag(z);
    memset(slot, 0, CCCC_CX_BYTES);
    memcpy(slot, &re, sizeof re);
    memcpy(slot + CCCC_LD_SIZE, &im, sizeof im);
    return (long long)(intptr_t)slot;
}

#define CCCC_CX_UNARY(NAME)                                                    \
    static long long cx_##NAME(long long z) {                                  \
        return cx_put_d(NAME(cx_get_d(z)));                                    \
    }                                                                          \
    static long long cx_##NAME##f(long long z) {                               \
        return cx_put_f(NAME##f(cx_get_f(z)));                                 \
    }                                                                          \
    static long long cx_##NAME##l(long long z) {                               \
        return cx_put_l(NAME(cx_get_l(z)));                                    \
    }

CCCC_CX_UNARY(cacos)
CCCC_CX_UNARY(cacosh)
CCCC_CX_UNARY(casin)
CCCC_CX_UNARY(casinh)
CCCC_CX_UNARY(catan)
CCCC_CX_UNARY(catanh)
CCCC_CX_UNARY(ccos)
CCCC_CX_UNARY(ccosh)
CCCC_CX_UNARY(cexp)
CCCC_CX_UNARY(clog)
CCCC_CX_UNARY(conj)
CCCC_CX_UNARY(cproj)
CCCC_CX_UNARY(csin)
CCCC_CX_UNARY(csinh)
CCCC_CX_UNARY(csqrt)
CCCC_CX_UNARY(ctan)
CCCC_CX_UNARY(ctanh)
#undef CCCC_CX_UNARY

static long long cx_cpow(long long z, long long w) {
    return cx_put_d(cpow(cx_get_d(z), cx_get_d(w)));
}
static long long cx_cpowf(long long z, long long w) {
    return cx_put_f(cpowf(cx_get_f(z), cx_get_f(w)));
}
static long long cx_cpowl(long long z, long long w) {
    return cx_put_l(cpow(cx_get_l(z), cx_get_l(w)));
}

#define CCCC_CX_REAL(NAME)                                                     \
    static double cx_##NAME(long long z) {                                     \
        return NAME(cx_get_d(z));                                              \
    }                                                                          \
    static float cx_##NAME##f(long long z) {                                   \
        return NAME##f(cx_get_f(z));                                           \
    }                                                                          \
    static double cx_##NAME##l(long long z) {                                  \
        return NAME(cx_get_l(z));                                              \
    }

CCCC_CX_REAL(cabs)
CCCC_CX_REAL(carg)
CCCC_CX_REAL(cimag)
CCCC_CX_REAL(creal)
#undef CCCC_CX_REAL

void register_complex_functions(VirtualMachine *vm) {
#define CCCC_REG_CX(NAME, NARGS, RET)                                          \
    cc_register_cfunc(vm, #NAME, (void *)cx_##NAME, NARGS, RET);               \
    cc_register_cfunc(vm, #NAME "f", (void *)cx_##NAME##f, NARGS,              \
                      RET == 1 ? 2 : RET);                                     \
    cc_register_cfunc(vm, #NAME "l", (void *)cx_##NAME##l, NARGS, RET);
    CCCC_REG_CX(cacos, 1, 0)
    CCCC_REG_CX(cacosh, 1, 0)
    CCCC_REG_CX(casin, 1, 0)
    CCCC_REG_CX(casinh, 1, 0)
    CCCC_REG_CX(catan, 1, 0)
    CCCC_REG_CX(catanh, 1, 0)
    CCCC_REG_CX(ccos, 1, 0)
    CCCC_REG_CX(ccosh, 1, 0)
    CCCC_REG_CX(cexp, 1, 0)
    CCCC_REG_CX(clog, 1, 0)
    CCCC_REG_CX(conj, 1, 0)
    CCCC_REG_CX(cpow, 2, 0)
    CCCC_REG_CX(cproj, 1, 0)
    CCCC_REG_CX(csin, 1, 0)
    CCCC_REG_CX(csinh, 1, 0)
    CCCC_REG_CX(csqrt, 1, 0)
    CCCC_REG_CX(ctan, 1, 0)
    CCCC_REG_CX(ctanh, 1, 0)
    CCCC_REG_CX(cabs, 1, 1)
    CCCC_REG_CX(carg, 1, 1)
    CCCC_REG_CX(cimag, 1, 1)
    CCCC_REG_CX(creal, 1, 1)
#undef CCCC_REG_CX
}
