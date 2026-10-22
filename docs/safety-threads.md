# Threads and Floating-Point Traps

Thread safety and floating-point division traps.

## Threading Safety

The VM runs one guest thread at a time under a global lock; see [threading.md](threading.md). The following threading-aware features are available:

**GIL release around blocking POSIX calls**

Blocking calls — `read`, `write`, `pread`, `pwrite`, `readv`, `writev`, `preadv`, `pwritev`, `poll`, `accept`, `connect`, `wait`, `waitpid`, `sleep`, `usleep` — release the GIL while blocked so other VM threads can make progress. Non-blocking calls (`close`, `lseek`, `stat`, `open`, and most control-plane socket calls) intentionally hold the GIL.

**Thread-aware stack canaries**

Stack canary checks work correctly under threading. Each VM thread runs with its own `bp`/`sp`/`stack_seg` via `ExecState` (saved and restored on every GIL hand-off), so canary slots are isolated per thread. Stack canary protection enabled with `-1` or `--stack-canaries` is not disabled when `pthread_create` is used.

**Dangling-pointer detection under threading**

`--dangling-pointers`'s per-frame liveness bookkeeping (`frame_epochs`/`live_epochs`/`stack_ptr_epochs`/`stack_intervals`, see below) is isolated per thread the same way stack canaries are: each field lives in `ExecState` and is swapped in/out on every GIL hand-off, so a worker thread's own `ENT3`/`LEV3` bookkeeping is never compared against another thread's unrelated stack. Before this, running any worker thread at `-3`/`--dangling-pointers` while another thread had an outstanding escaping local (e.g. simply because `main` took `&thread_id` for `pthread_create`'s out-parameter) would assert almost immediately. Detection of a pointer escaping **across** threads (a worker's local address stored somewhere and dereferenced by a different thread after the worker has exited) is not implemented — each thread's liveness bookkeeping is discarded when that thread exits, so such a dereference is a real, undetected use-after-free rather than a diagnosed one.

**`--thread-safety` diagnostics** _(off by default; enables the checks below)_

Enable with `--thread-safety`. Intended for development and testing — not enabled by preset safety levels.

- **Double-lock detection**: Diagnoses when a thread attempts to lock a non-recursive mutex it already holds.
  ```
  ====== DEADLOCK: double-lock detected ======
  ```

- **Lock-order inversion detection**: Builds a lock-acquisition graph at runtime. When a thread acquires locks in the opposite order seen previously, a potential deadlock is reported.
  ```
  ====== LOCK ORDER INVERSION detected ======
  ```

- **Data race detection**: Tracks the last thread to write each address without holding a mutex (shadow-map approach). When a second thread accesses the same address also without a mutex, a potential race is reported. Detection covers pointer-dereference and indexed array access; global-variable access tracking is planned for a future release.
  ```
  ====== DATA RACE DETECTED ======
  ```

- **`_Atomic` cast warning**: Warns at compile time when a cast strips the `_Atomic` qualifier from a pointer type, producing a plain pointer to an atomic object.
  ```
  warning: cast discards '_Atomic' qualifier from pointer type; non-atomic access to atomic object may cause data races
  ```

- **Mixed atomic/non-atomic access detection**: `atomic_load`/`atomic_store` (and `atomic_load_explicit`/`atomic_store_explicit`) emit dedicated `ALDR`/`ASTR` opcodes that tag a per-address `atomic_shadow` map with the accessing thread id. If a second thread performs a plain (non-atomic) memory access to the same address without holding a mutex, a diagnostic is printed at runtime. Forward-only detection: the atomic access must occur before the non-atomic one to be detected.
  ```
  ====== MIXED ATOMIC/NON-ATOMIC ACCESS DETECTED ======
  Address 0x... was accessed atomically by thread 0x... and is now read non-atomically by thread 0x... without a mutex
  ```
  `atomic_fetch_add`/`sub`/`or`/`xor`/`and` route through a CAS retry loop over the same `ACAS` opcode `atomic_compare_exchange_*` uses, so they set the atomic tag exactly as `atomic_load`/`atomic_store` do — mixing `atomic_fetch_*` with `atomic_store`/`atomic_load` on the same address across threads is correctly tracked, not a known limitation.

- **`atomic_exchange` and `atomic_compare_exchange`**: These operations compile to dedicated `AXCHG`/`ACAS` opcodes and are correctly handled as atomic accesses in the shadow map. The VM GIL ensures atomicity with respect to other VM threads.


## Floating-Point Behavior


### `--trap-fp-divzero` — Abort on float division by zero

- By default, `float`/`double` division by zero follows IEEE-754: a
  finite value divided by `0.0` produces a correctly-signed infinity
  (and raises `FE_DIVBYZERO`, observable via `<fenv.h>`), and `0.0/0.0`
  produces NaN (raises `FE_INVALID`). Neither is undefined behavior,
  unlike integer division by zero, so this is not gated by any `-0`
  through `-3` safety tier and is not part of `CCCC_ALL_SAFETY`.
- With this flag, any zero divisor in a float/double division aborts
  execution with the same `DIVISION BY ZERO` diagnostic used by integer
  division and `--overflow-checks`, for debugging code that assumes
  division by zero is always a bug.
