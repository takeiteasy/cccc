# Threads and Floating-Point Traps

Thread-aware safety, `--thread-safety` diagnostics, and `--trap-fp-divzero`.

## Threads

The VM runs one guest thread at a time under a global lock ([threading.md](threading.md)).

| Feature | Behaviour |
|---|---|
| Blocking POSIX calls | `read`, `write`, `pread`, `pwrite`, `readv`, `writev`, `preadv`, `pwritev`, `poll`, `accept`, `connect`, `wait`, `waitpid`, `sleep`, `usleep` release the lock while blocked. Non-blocking calls (`close`, `lseek`, `stat`, `open`, most socket control calls) hold it |
| Stack canaries | Each thread has its own stack state, so canary slots are isolated and stay on under `pthread_create` |
| Dangling-pointer bookkeeping | Isolated per thread the same way. A pointer escaping *across* threads (a worker's local address dereferenced by another thread after the worker exits) is not detected |

## `--thread-safety`

Off by default and not part of any preset; for development and testing.

| Check | Reported as |
|---|---|
| A thread locks a non-recursive mutex it already holds | `====== DEADLOCK: double-lock detected ======` |
| Locks acquired in the opposite order to one seen before | `====== LOCK ORDER INVERSION detected ======` |
| Two threads access an address without a mutex (pointer dereference and indexed array access; globals are not tracked yet) | `====== DATA RACE DETECTED ======` |
| A cast strips `_Atomic` from a pointer | Compile-time: `warning: cast discards '_Atomic' qualifier from pointer type; non-atomic access to atomic object may cause data races` |
| A plain access to an address another thread accessed atomically, without a mutex | `====== MIXED ATOMIC/NON-ATOMIC ACCESS DETECTED ======` |

```
====== MIXED ATOMIC/NON-ATOMIC ACCESS DETECTED ======
Address 0x... was accessed atomically by thread 0x... and is now read non-atomically by thread 0x... without a mutex
```

Mixed-access detection is forward-only: the atomic access must come first. `atomic_load`, `atomic_store` and the `_explicit` forms tag the address, as do `atomic_fetch_add/sub/or/xor/and` (a compare-exchange loop) and `atomic_exchange` / `atomic_compare_exchange`, so mixing those on one address across threads is tracked.

## `--trap-fp-divzero`

By default float and double division follows IEEE-754: a finite value over `0.0` gives a correctly signed infinity (and raises `FE_DIVBYZERO`), `0.0/0.0` gives NaN (and raises `FE_INVALID`). Neither is undefined behaviour, so this is not in any safety level. With the flag, a zero divisor aborts with the same `DIVISION BY ZERO` diagnostic as integer division and `--overflow-checks`.
