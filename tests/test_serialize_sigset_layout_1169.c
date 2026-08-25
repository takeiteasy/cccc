// Ticket #1169, follow-up to #1168: under -c=native, `sizeof`/`_Alignof` of a
// `from_include` **scalar** typedef (as opposed to a struct/union/enum) must
// re-materialize textually against the real host header, exactly like an
// aggregate does -- CCCC's own `include/signal.h` types `sigset_t` as a plain
// `unsigned int` (4 bytes, sized for the VM's own set-manipulation functions,
// see #738), but the real host `sigset_t` is a different size on some hosts
// (128 bytes on glibc). #1168 fixed a spurious-scalar-member false positive
// in `type_layout_is_host_owned()` by restricting it to
// TY_STRUCT/TY_UNION/TY_ENUM outright, which collaterally stopped a genuinely
// divergent scalar typedef like `sigset_t` from re-materializing too --
// reintroducing the #1031 hazard class for it. #1169 restores the scalar case
// via `find_typedef_name_exact()`'s pointer-identity lookup (parse_typedef()
// already `copy_type()`s every non-aggregate typedef so it has its own `Type`
// identity distinct from the bare builtin it aliases -- no structural match
// needed, so #1168's spurious-match bug can't reopen).
//
// This test can't assert a specific byte count for `sizeof(sigset_t)` --
// legitimately 4 under the VM and platform-dependent under -c=native (4 on
// macOS, 128 on glibc) -- see tools/comptime_native_smoke.py's own case for
// the output-text assertion that is the real regression guard for the fix
// itself. What this test can and does assert, on both backends alike: a
// buffer sized off `sizeof(sigset_t)` is exactly big enough for every
// `sig*set()` call to touch, with no overrun into a trailing canary region --
// the same "undersized buffer" shape test_serialize_layout_const_1031.c uses
// for pthread_mutex_t.

#include <signal.h>

extern void *malloc(unsigned long size);
extern void free(void *ptr);
extern void *memset(void *s, int c, unsigned long n);

int main(void) {
    unsigned long guest_size = sizeof(sigset_t);
    unsigned long align      = _Alignof(sigset_t);
    if (align == 0 || (align & (align - 1)) != 0)
        return 1; // alignment must be a nonzero power of two on every host

    unsigned long  tail = 64;
    unsigned char *buf  = (unsigned char *)malloc(guest_size + tail);
    if (!buf)
        return 2;
    memset(buf, 0, guest_size + tail);
    memset(buf + guest_size, 0xAA, tail);

    sigset_t *set = (sigset_t *)buf;
    if (sigemptyset(set) != 0) {
        free(buf);
        return 3;
    }
    if (sigaddset(set, 2) != 0) { // SIGINT on every supported host
        free(buf);
        return 4;
    }
    if (sigismember(set, 2) != 1) {
        free(buf);
        return 5;
    }
    if (sigdelset(set, 2) != 0) {
        free(buf);
        return 6;
    }
    if (sigfillset(set) != 0) {
        free(buf);
        return 7;
    }

    for (unsigned long i = guest_size; i < guest_size + tail; i++) {
        if (buf[i] != 0xAA) {
            free(buf);
            return 8; // canary clobbered: a real host sig*set() call overran
                      // a buffer sized off a stale guest-folded
                      // sizeof(sigset_t)
        }
    }
    free(buf);

    // struct member case: sizeof(sigset_t) reached through an aggregate
    // member must also re-materialize (chosen scope for #1169).
    struct Wrap1169 {
        sigset_t s;
    };
    if (sizeof(struct Wrap1169) < sizeof(sigset_t))
        return 9;

    return 42;
}
