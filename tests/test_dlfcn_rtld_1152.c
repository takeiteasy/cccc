// Ticket #1152: include/dlfcn.h's RTLD_LOCAL/RTLD_GLOBAL used to be
// hardcoded at glibc's numeric encoding (0x0/0x100) on every platform.
// On macOS the real values are 0x4/0x8, and 0x100 is RTLD_FIRST there --
// a different flag entirely, not just a different number for the same
// flag -- so a guest asking for RTLD_GLOBAL on macOS actually passed
// RTLD_FIRST to the real host dlopen(). Fixed by deriving every RTLD_*
// value from the real host <dlfcn.h> this cccc binary was built against
// (init_dlfcn_macros(), src/preprocess.c) instead of hand-transcribing
// them, the same pattern init_errno_macros() uses for errno.h (#779/#813).
//
// This test pins the host's real numbers per platform, which is what
// actually catches broken wiring (injection missing, header alias stale,
// src/std.c not regenerated) now that the values themselves are no longer
// hand-transcribed. Deliberately no bare #else arm on the platform split:
// CCCC also predefines __FreeBSD__/__NetBSD__/__sun for guests
// (src/preprocess.c), and a fallback arm here would just reintroduce, in
// the test, the transcribed table the header itself stopped carrying.
#include <dlfcn.h>
#include <stdio.h>

#if defined(__APPLE__)
_Static_assert(RTLD_LAZY == 0x1, "RTLD_LAZY");
_Static_assert(RTLD_NOW == 0x2, "RTLD_NOW");
_Static_assert(RTLD_LOCAL == 0x4, "RTLD_LOCAL");
_Static_assert(RTLD_GLOBAL == 0x8, "RTLD_GLOBAL");
_Static_assert(RTLD_NOLOAD == 0x10, "RTLD_NOLOAD");
_Static_assert(RTLD_NODELETE == 0x80, "RTLD_NODELETE");
_Static_assert(RTLD_FIRST == 0x100, "RTLD_FIRST");
#ifdef RTLD_DEEPBIND
#error "RTLD_DEEPBIND should not exist on macOS (#824 no-lossy-emulation)"
#endif
#elif defined(__linux__)
_Static_assert(RTLD_LAZY == 0x1, "RTLD_LAZY");
_Static_assert(RTLD_NOW == 0x2, "RTLD_NOW");
_Static_assert(RTLD_LOCAL == 0, "RTLD_LOCAL");
_Static_assert(RTLD_GLOBAL == 0x100, "RTLD_GLOBAL");
_Static_assert(RTLD_NOLOAD == 0x4, "RTLD_NOLOAD");
_Static_assert(RTLD_DEEPBIND == 0x8, "RTLD_DEEPBIND");
_Static_assert(RTLD_NODELETE == 0x1000, "RTLD_NODELETE");
_Static_assert(RTLD_BINDING_MASK == 0x3, "RTLD_BINDING_MASK");
#ifdef RTLD_FIRST
#error "RTLD_FIRST should not exist on Linux (#824 no-lossy-emulation)"
#endif
#endif

int main(void) {
    // Behavioural round trip through both backends: RTLD_GLOBAL/RTLD_LOCAL
    // now carry the host's real values, so a symbol resolved through
    // either should still work.
    void *g = dlopen(0, RTLD_NOW | RTLD_GLOBAL);
    if (!g)
        return 1;
    if (!dlsym(g, "printf"))
        return 2;

    void *l = dlopen(0, RTLD_NOW | RTLD_LOCAL);
    if (!l)
        return 3;
    if (!dlsym(l, "printf"))
        return 4;

    // Bare RTLD_LOCAL, no explicit binding mode: on macOS RTLD_LOCAL is
    // now 0x4 (not glibc's 0x0), so `mode ? mode : RTLD_LAZY`
    // (cccc_rt_dlopen, src/vm.c; the emitted -c=native shim,
    // src/serialize_shims.c) no longer falls back to RTLD_LAZY here --
    // it calls the real dlopen() with RTLD_LOCAL and neither RTLD_LAZY
    // nor RTLD_NOW set. Confirms that still succeeds on both backends.
    void *bare = dlopen(0, RTLD_LOCAL);
    if (!bare)
        return 5;

    return 42;
}
