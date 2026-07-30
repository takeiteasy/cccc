// CCCC_FLAGS: -3
// Expected return: 42
// #866: the dangling-detector's per-frame liveness bookkeeping
// (frame_epochs/live_epochs/stack_ptr_epochs/stack_intervals) used to be
// VM-wide state, never isolated across a thread switch -- since each
// pthread/thrd worker gets its own, separately-mmap'd stack (a disjoint
// address range from main's), comparing a worker thread's vm->bp against
// frame_epochs entries pushed under the MAIN thread's bp (e.g. because main
// took the address of a local `pthread_t`/`thrd_t` to pass to
// pthread_create/thrd_create, or is blocked inside pthread_join with its own
// escaping-local bookkeeping still outstanding) asserted almost immediately
// -- not specific to TSS destructors, just to running any worker thread at
// all under -3 while any other thread has an outstanding escaping local.
// This is now fixed by folding those fields into ExecState, swapped
// per-thread exactly like regs/pc/sp/bp already are.
#include <pthread.h>

static void *worker(void *arg) {
    // An escaping local in the WORKER's own frame: forces ENT3 to push a
    // frame_epochs entry using the worker's own (private-stack) bp.
    int local = 7;
    int *volatile escaped = &local;
    return (void *)(long)*escaped;
}

int main(void) {
    // An escaping local in MAIN's own frame too, still outstanding (main's
    // own ENT3 pushed its frame_epochs entry using main's stack) while the
    // worker thread runs and returns -- this is exactly the interleaving
    // that desynced the VM-wide bookkeeping before the fix.
    pthread_t t;
    pthread_t *volatile t_ptr = &t;

    if (pthread_create(t_ptr, NULL, worker, NULL) != 0)
        return 1;

    void *retval = NULL;
    if (pthread_join(*t_ptr, &retval) != 0)
        return 2;

    return (int)(long)retval == 7 ? 42 : 3;
}
