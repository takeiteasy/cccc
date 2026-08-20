// Ticket #1031: under -c=native, member access through a from_include
// struct (e.g. `pthread_mutex_t`, `struct statfs`) correctly re-resolves
// against the real host header the replayed `#include` pulls in -- but a
// `sizeof`/`_Alignof` of that same type that guest-side parsing already
// folded into a plain integer literal (CCCC's own, generally smaller,
// projection) is not retroactively fixed by that suppression, and stays
// stale in the emitted TU.
//
// This test proves the fix with `pthread_mutex_t`, not `struct statfs`
// (the ticket's own last remaining file, see test_sys_mount_statfs.c):
// include/pthread.h already has a real #include_next hand-off to the host
// header (#1022), so under the test harness's own `-I./include` invocation
// the real host pthread_mutex_t genuinely wins at native-compile time --
// giving a large, previously-measured divergence to check against (CCCC's
// own projection is a 24-byte {void*,long,int} triple; real macOS arm64's
// is 64 bytes, see #1022's own writeup). A malloc'd buffer sized off a
// guest-folded `sizeof(pthread_mutex_t)` -- pre-fix, a bare `24ULL` in the
// emitted C -- is undersized once the real host pthread_mutex_init()
// writes its own real-sized structure into it; post-fix, the emitted C
// re-materializes `sizeof(pthread_mutex_t)` textually, so it's the host
// compiler's own (correct) evaluation that lands in the binary.
//
// Also exercises `sizeof(...) + N` (only the sizeof half folds -- the
// surrounding arithmetic does not, confirmed empirically while fixing this
// ticket) and `_Alignof`, folded the same way and covered by the same fix.

#include <pthread.h>

extern void *malloc(unsigned long size);
extern void free(void *ptr);
extern void *memset(void *s, int c, unsigned long n);

int main(void) {
    unsigned long guest_size = sizeof(pthread_mutex_t);
    unsigned long align      = _Alignof(pthread_mutex_t);
    if (align == 0 || (align & (align - 1)) != 0)
        return 1; // alignment must be a nonzero power of two on every host

    unsigned long  tail = 64;
    unsigned char *buf  = (unsigned char *)malloc(guest_size + tail);
    if (!buf)
        return 2;
    // Zero the mutex region itself (CCCC's own VM-side mutex is lazily
    // heap-allocated on first lock, and a non-zero handle field reads as
    // "already initialized" -- pthread_mutex_init() correctly refuses to
    // clobber it, EBUSY -- so poisoning it here would fail the VM run for
    // a reason unrelated to this ticket), poison only the tail past it.
    memset(buf, 0, guest_size);
    memset(buf + guest_size, 0xAA, tail);

    pthread_mutex_t *m = (pthread_mutex_t *)buf;
    if (pthread_mutex_init(m, 0) != 0) {
        free(buf);
        return 3;
    }
    if (pthread_mutex_lock(m) != 0) {
        pthread_mutex_destroy(m);
        free(buf);
        return 4;
    }
    if (pthread_mutex_unlock(m) != 0) {
        pthread_mutex_destroy(m);
        free(buf);
        return 5;
    }
    pthread_mutex_destroy(m);

    for (unsigned long i = guest_size; i < guest_size + tail; i++) {
        if (buf[i] != 0xAA) {
            free(buf);
            return 6; // canary clobbered: real init/lock overran the
                      // guest-folded buffer size
        }
    }
    free(buf);

    // sizeof(...) + N: the sizeof half re-materializes, the "+ N" stays a
    // plain folded literal (arithmetic around a layout constant is never
    // folded away in the first place -- confirmed empirically, see the
    // ticket's own note -- so nothing extra is needed here, but this
    // guards against a future change accidentally collapsing the whole
    // expression back into one bare literal).
    unsigned long combined = sizeof(pthread_mutex_t) + 8;
    if (combined != guest_size + 8)
        return 7;

    return 42;
}
