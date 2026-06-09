// fenv.h stdlib function registration
#include "../cccc.h"
#include <fenv.h>

static long long wrap_feclearexcept(long long excepts) { return (long long)feclearexcept((int)excepts); }
static long long wrap_fegetexceptflag(long long flagp, long long excepts) { return (long long)fegetexceptflag((fexcept_t *)flagp, (int)excepts); }
static long long wrap_feraiseexcept(long long excepts) { return (long long)feraiseexcept((int)excepts); }
static long long wrap_fesetexceptflag(long long flagp, long long excepts) { return (long long)fesetexceptflag((const fexcept_t *)flagp, (int)excepts); }
static long long wrap_fetestexcept(long long excepts) { return (long long)fetestexcept((int)excepts); }
static long long wrap_fegetround(void) { return (long long)fegetround(); }
static long long wrap_fesetround(long long round) { return (long long)fesetround((int)round); }
static long long wrap_fegetenv(long long envp) { return (long long)fegetenv((fenv_t *)envp); }
static long long wrap_feholdexcept(long long envp) { return (long long)feholdexcept((fenv_t *)envp); }
static long long wrap_fesetenv(long long envp) { return (long long)fesetenv((const fenv_t *)envp); }
static long long wrap_feupdateenv(long long envp) { return (long long)feupdateenv((const fenv_t *)envp); }

void register_fenv_functions(CCCC *vm) {
    cc_register_cfunc(vm, "feclearexcept", (void*)wrap_feclearexcept, 1, 0);
    cc_register_cfunc(vm, "fegetexceptflag", (void*)wrap_fegetexceptflag, 2, 0);
    cc_register_cfunc(vm, "feraiseexcept", (void*)wrap_feraiseexcept, 1, 0);
    cc_register_cfunc(vm, "fesetexceptflag", (void*)wrap_fesetexceptflag, 2, 0);
    cc_register_cfunc(vm, "fetestexcept", (void*)wrap_fetestexcept, 1, 0);
    cc_register_cfunc(vm, "fegetround", (void*)wrap_fegetround, 0, 0);
    cc_register_cfunc(vm, "fesetround", (void*)wrap_fesetround, 1, 0);
    cc_register_cfunc(vm, "fegetenv", (void*)wrap_fegetenv, 1, 0);
    cc_register_cfunc(vm, "feholdexcept", (void*)wrap_feholdexcept, 1, 0);
    cc_register_cfunc(vm, "fesetenv", (void*)wrap_fesetenv, 1, 0);
    cc_register_cfunc(vm, "feupdateenv", (void*)wrap_feupdateenv, 1, 0);
}
