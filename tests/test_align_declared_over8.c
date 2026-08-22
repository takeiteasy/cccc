// Expected return: 42
// #1136: declared alignment > 8 -- via explicit _Alignas, __int128's
// natural 16-byte alignment (#1135), and a vector type's natural alignment
// (#722) -- must be honoured by the data-segment/TLS allocator, not
// silently rounded to a hardcoded 8. Odd-sized padding globals are
// interleaved between the over-aligned ones so this test actually
// discriminates: with the pre-#1136 hardcoded 8-byte rounding, at least one
// of g_align32/g_int128/g_vector32/g_align16 below would land on an address
// that is 8-aligned but not aligned to its own declared/natural alignment.
#include <threads.h>

typedef float v8f32 __attribute__((vector_size(32)));

char          pad1;
_Alignas(32) int g_align32;
char     pad2[3];
__int128 g_int128;
char     pad3;
v8f32    g_vector32;
char     pad4[5];
_Alignas(16) long long g_align16;

_Thread_local char t_pad1;
_Thread_local _Alignas(16) long long t_align16;
_Thread_local char     t_pad2[3];
_Thread_local __int128 t_int128;

// Returns a vector by value (#714), exercising the RETBUF pool's own
// alignment (alloc_return_buffer_pool, src/codegen_func.c) -- the
// FFI-visible case #1136 is most concerned about. Not independently
// assertable from here: the returned value's *local* address (`&rv` at the
// call site) is a stack slot, and >8-byte local alignment is the deferred
// half of #1136 (see the ticket's own follow-up) -- so this only smoke-
// tests that the call still works, it doesn't assert RETBUF's own base
// alignment.
static v8f32 make_vector(void) {
    v8f32 v = {0};
    return v;
}

static int misaligned(void *p, int align) {
    return (unsigned long long)p % (unsigned long long)align != 0;
}

static int check_globals(void) {
    if (misaligned(&g_align32, 32))
        return 2;
    if (misaligned(&g_int128, 16))
        return 3;
    if (misaligned(&g_vector32, 32))
        return 4;
    if (misaligned(&g_align16, 16))
        return 5;
    return 0;
}

static int check_tls(void) {
    if (misaligned(&t_align16, 16))
        return 6;
    if (misaligned(&t_int128, 16))
        return 7;
    return 0;
}

static int worker(void *arg) {
    (void)arg;
    int rc = check_tls();
    thrd_exit(rc);
}

int main(void) {
    int rc = check_globals();
    if (rc)
        return rc;

    // Main thread's own TLS copy.
    rc = check_tls();
    if (rc)
        return rc;

    // A second thread's TLS copy -- the per-thread base (not just the
    // template) must carry the same alignment.
    thrd_t th;
    if (thrd_create(&th, worker, NULL) != thrd_success)
        return 10;
    int wrc = 0;
    thrd_join(th, &wrc);
    if (wrc)
        return wrc + 10; // distinguish from the main-thread failure codes

    v8f32 rv = make_vector();
    (void)rv;

    return 42;
}
