// fenv.h stdlib function registration
#include "../cccc.h"
#include <fenv.h>
#include <stdint.h>

// include/fenv.h defines the guest's FE_DFL_ENV as ((const fenv_t *)-1), a
// sentinel guest code can pass through fesetenv()/feupdateenv() without
// knowing the real host FE_DFL_ENV value. That's necessary because on some
// platforms (macOS) the real FE_DFL_ENV is an opaque host address
// (&_FE_DFL_ENV), not representable as a portable guest-side constant.
// Substitute the host's real FE_DFL_ENV whenever the sentinel comes through,
// rather than dereferencing -1 (#771).
static const fenv_t *resolve_envp(long long envp) {
    if (envp == -1)
        return FE_DFL_ENV;
    return (const fenv_t *)(intptr_t)envp;
}

static long long wrap_feclearexcept(long long excepts) {
    return (long long)feclearexcept((int)excepts);
}
static long long wrap_fegetexceptflag(long long flagp, long long excepts) {
    return (long long)fegetexceptflag((fexcept_t *)(intptr_t)flagp,
                                      (int)excepts);
}
static long long wrap_feraiseexcept(long long excepts) {
    return (long long)feraiseexcept((int)excepts);
}
static long long wrap_fesetexceptflag(long long flagp, long long excepts) {
    return (long long)fesetexceptflag((const fexcept_t *)(intptr_t)flagp,
                                      (int)excepts);
}
static long long wrap_fetestexcept(long long excepts) {
    return (long long)fetestexcept((int)excepts);
}
static long long wrap_fegetround(void) {
    return (long long)fegetround();
}
static long long wrap_fesetround(long long round) {
    return (long long)fesetround((int)round);
}
static long long wrap_fegetenv(long long envp) {
    return (long long)fegetenv((fenv_t *)(intptr_t)envp);
}
static long long wrap_feholdexcept(long long envp) {
    return (long long)feholdexcept((fenv_t *)(intptr_t)envp);
}
static long long wrap_fesetenv(long long envp) {
    return (long long)fesetenv(resolve_envp(envp));
}
static long long wrap_feupdateenv(long long envp) {
    return (long long)feupdateenv(resolve_envp(envp));
}

// FLT_ROUNDS (<float.h>, C11 dynamic requirement) maps the host's current
// rounding mode -- as last set by fesetround() -- to the encoding C requires:
// 0 toward-zero, 1 to-nearest, 2 toward +inf, 3 toward -inf, -1 indeterminate.
long long __cccc_flt_rounds(void) {
    switch (fegetround()) {
        case FE_TOWARDZERO:
            return 0;
        case FE_TONEAREST:
            return 1;
        case FE_UPWARD:
            return 2;
        case FE_DOWNWARD:
            return 3;
        default:
            return -1;
    }
}

void register_fenv_functions(VirtualMachine *vm) {
    cc_register_cfunc(vm, "feclearexcept", (void *)wrap_feclearexcept, 1, 0);
    cc_register_cfunc(vm, "fegetexceptflag", (void *)wrap_fegetexceptflag, 2,
                      0);
    cc_register_cfunc(vm, "feraiseexcept", (void *)wrap_feraiseexcept, 1, 0);
    cc_register_cfunc(vm, "fesetexceptflag", (void *)wrap_fesetexceptflag, 2,
                      0);
    cc_register_cfunc(vm, "fetestexcept", (void *)wrap_fetestexcept, 1, 0);
    cc_register_cfunc(vm, "fegetround", (void *)wrap_fegetround, 0, 0);
    cc_register_cfunc(vm, "fesetround", (void *)wrap_fesetround, 1, 0);
    cc_register_cfunc(vm, "fegetenv", (void *)wrap_fegetenv, 1, 0);
    cc_register_cfunc(vm, "feholdexcept", (void *)wrap_feholdexcept, 1, 0);
    cc_register_cfunc(vm, "fesetenv", (void *)wrap_fesetenv, 1, 0);
    cc_register_cfunc(vm, "feupdateenv", (void *)wrap_feupdateenv, 1, 0);
    cc_register_cfunc(vm, "__cccc_flt_rounds", (void *)__cccc_flt_rounds, 0, 0);
}
