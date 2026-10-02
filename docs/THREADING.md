# Threading

Guest threads work in the VM and in `-c=native`, but they run differently. The
VM runs one thread at a time under a global lock. Native output runs threads in
parallel on real host threads.

```c
#include <pthread.h>

static void *work(void *arg) { *(int *)arg = 42; return NULL; }

int main(void) {
    int out = 0;
    pthread_t t;
    pthread_create(&t, NULL, work, &out);
    pthread_join(t, NULL);
    return out;
}
```

```sh
cccc threads.c               # VM: threads take turns
cccc -c=native threads.c     # native: threads run in parallel
```

## VM and native

| | VM | `-c=native`, `-m`, `-c=generated` |
|---|---|---|
| Execution | One thread at a time | Parallel |
| `<pthread.h>` | VM-managed handles over host pthreads | The real host `<pthread.h>`[^1] |
| `<threads.h>` | VM wrappers | A shim over the host `<pthread.h>`[^2] |
| `_Thread_local`, `__thread` | Private copy per thread | Host thread-local storage |
| `<stdatomic.h>` | Real atomic operations | Real atomic operations |
| Data races | Mostly hidden | Real |

## The global lock

The VM holds a recursive global interpreter lock (GIL) while it runs bytecode.
A thread gives the lock up only when it blocks, so threads take turns at those
points and never in the middle of an expression.[^3]

These release the lock while they wait:

| Area | Calls |
|---|---|
| Threads | `pthread_join`, `thrd_join`, `thrd_sleep`, `thrd_yield` |
| Locks | `pthread_mutex_lock`, `pthread_cond_wait` and `pthread_cond_timedwait`, `mtx_lock`, `cnd_wait` |
| File and socket I/O | `read`, `write`, `pread`, `pwrite`, `readv`, `writev`, `poll`, `select`, `accept`, `connect` |
| Processes and time | `wait`, `waitpid`, `sleep`, `usleep` |
| Other blocking calls | Name lookups, message queues, SysV semaphores and queues, `aio_suspend` |

Quick calls such as `close`, `lseek`, `stat` and `open` keep the lock.

### Spinning

A thread that waits in a loop without calling anything that blocks never gives
the lock up, so the thread it waits for never runs and the program hangs:

```c
while (!atomic_load(&flag)) {}               // hangs in the VM
while (!atomic_load(&flag)) thrd_yield();    // works
```

Wait with a mutex and condition variable, or call `thrd_yield()` in the loop.

## What the lock gives

- Blocking and wakeup behave as POSIX describes: `pthread_join` waits,
  `pthread_cond_wait` wakes on a signal, a recursive mutex counts its locks.
- Each thread has its own stack, stack canaries and dangling-pointer
  bookkeeping.[^4]
- Atomic operations stay atomic. `atomic_fetch_add`, `atomic_load` and the rest
  are real atomic operations on both back ends, with no lock needed.
- There is no parallel speed-up. Use `-c=native` for that.

## Thread-local storage

`_Thread_local`, `__thread` and C23 `thread_local` give each thread a private
copy that starts from the variable's initialiser. `pthread_key_create` and
`tss_create` keys work with the usual destructors.

## Race and deadlock checks

`--thread-safety` turns on VM diagnostics for double-locks, lock-order
inversion, unsynchronised access from two threads and mixing atomic with plain
access. See [SAFETY.md](SAFETY.md#threading-safety).

The VM hides many races because only one thread runs at a time. Code that
passes in the VM can still race under `-c=native`. Build the native output with
`-fsanitize=thread` to find them.

## Other threading features

| Feature | Guide |
|---|---|
| `#pragma omp` parallel regions | [OPENMP.md](OPENMP.md) |
| `[[cccc::kernel]]` work-items | [KERNELS.md](KERNELS.md) |
| `<pthread.h>`, `<threads.h>` and `<stdatomic.h>` coverage | [STDLIB.md](STDLIB.md) |
| `_Thread_local`, `_Atomic` coverage | [COVERAGE.md](COVERAGE.md) |

OpenMP regions and kernel launches run on one thread in the VM. Native output
runs them on its own thread pool.

## Limitations

- A thread that spins without calling anything that blocks hangs in the VM
  ([#1417](https://todo.sr.ht/~takeiteasy/cccc/1417)).

[^1]: `include/pthread.h` hands off to the host header, so the compiled program
    calls host pthread functions directly with no VM layer. See
    [HEADERS.md](HEADERS.md).
[^2]: The host's own `<threads.h>` is never used: Darwin has none, and glibc's
    error codes differ from CCCC's. `call_once` is a real function on both back
    ends. See [Serialized-output divergences](NATIVE.md#serialized-output-divergences).
[^3]: Signal handlers and `SIGEV_THREAD` notifications (`<aio.h>`, `<mqueue.h>`)
    run on a VM thread at a safe point in the dispatch loop. They never start a
    guest-visible pthread.
[^4]: A pointer to one thread's local that another thread dereferences after the
    first has exited is not detected by `--dangling-pointers`.
