// CCCC_FLAGS: --testing
// Addresses of functions the VM lowers to opcodes: signal, raise, longjmp, dl*.

#pragma cccc suite begin "intrinsic_addr"
#include <dlfcn.h>
#include <setjmp.h>
#include <signal.h>
#include <stddef.h>

typedef void (*handler_t)(int);
typedef handler_t (*signal_fn)(int, handler_t);

static volatile int hit;
static void on_signal(int s) {
    hit = s;
}

static signal_fn g_signal                     = signal;
static int (*g_raise)(int)                    = raise;
static void (*g_longjmp)(jmp_buf, int)        = longjmp;
static void *(*g_dlopen)(const char *, int)   = dlopen;
static void *(*g_dlsym)(void *, const char *) = dlsym;
static int (*g_dlclose)(void *)               = dlclose;
static char *(*g_dlerror)(void)               = dlerror;

[[cccc::test(return = 42)]]
int test_signal_raise_through_pointers(void) {
    hit = 0;
    g_signal(SIGUSR1, on_signal);
    if (g_raise(SIGUSR1) != 0 || hit != SIGUSR1)
        return 1;
    return 42;
}

[[cccc::test(return = 42)]]
int test_local_raise_pointer(void) {
    int (*lr)(int) = raise;
    hit            = 0;
    signal(SIGUSR2, on_signal);
    if (!lr || lr(SIGUSR2) != 0 || hit != SIGUSR2)
        return 1;
    return 42;
}

[[cccc::test(return = 42)]]
int test_longjmp_through_pointer(void) {
    jmp_buf jb;
    int     v = setjmp(jb);
    if (v == 0)
        g_longjmp(jb, 7);
    return v == 7 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_dl_through_pointers(void) {
    void *lib = g_dlopen(NULL, RTLD_NOW);
    if (!lib)
        return 1;
    if (!g_dlsym(lib, "strlen"))
        return 2;
    g_dlerror();
    void *again = g_dlopen(NULL, RTLD_NOW);
    if (!again || g_dlclose(again) != 0)
        return 3;
    return 42;
}
#pragma cccc suite end
