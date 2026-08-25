// Expected return: 42
// #1188: atomic_thread_fence/atomic_signal_fence used to expand to nothing
// (both empty macros in include/stdatomic.h). Harmless under the VM's GIL,
// but a real reordering hazard under -c=native's genuine thread parallelism
// -- code relying on a fence for publish/subscribe ordering had no guarantee
// at all. Fixed by lowering to a real __atomic_thread_fence/
// __atomic_signal_fence via the new ND_FENCE node.
//
// This is an *exercise* test, not a proof: on x86-64's TSO memory model a
// release/acquire fence pair is unlikely to expose a missing barrier even
// without the fix (TSO does not reorder stores after stores, or loads after
// loads), and the VM's GIL makes the VM side trivially ordered regardless.
// It only demonstrates the classic publish/subscribe pattern compiles and
// runs correctly on both backends. The actual proof that a real fence
// instruction is emitted lives in tools/comptime_native_smoke.py, which
// asserts on the -m output text.
#include <threads.h>
#include <stdatomic.h>

static int         g_payload = 0;
static _Atomic int g_ready   = 0;

static int publisher(void *arg) {
    (void)arg;
    g_payload = 99;                            // ordinary, non-atomic
    atomic_thread_fence(memory_order_release); // publish barrier
    atomic_store(&g_ready, 1);
    return 0;
}

static int subscriber(void *arg) {
    (void)arg;
    while (!atomic_load(&g_ready))
        thrd_yield();
    atomic_thread_fence(memory_order_acquire); // subscribe barrier
    return g_payload == 99 ? 0 : 1;
}

int main(void) {
    thrd_t pub, sub;
    int    sub_result = -1;

    if (thrd_create(&pub, publisher, NULL) != thrd_success)
        return 1;
    if (thrd_create(&sub, subscriber, NULL) != thrd_success)
        return 2;
    thrd_join(pub, 0);
    thrd_join(sub, &sub_result);

    if (sub_result != 0)
        return 3;

    return 42;
}
