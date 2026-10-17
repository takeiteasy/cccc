# Comptime Capabilities

Comptime code can call a safe subset of libc by default. Anything that touches files, processes, the network, the environment, shared libraries, threads, signals or IPC needs an explicit `--comptime-allow` on the command line. Running the compiled program is never restricted.

```sh
cccc prog.c                          # string, math, ctype, snprintf, malloc, printf
cccc --comptime-allow=fs prog.c      # + files and directories
cccc --comptime-allow=fs,exec prog.c # + running commands
cccc --comptime-allow=all prog.c     # everything the VM registers
```

## Classes

| Class | Grants | Examples |
|---|---|---|
| *(safe, always on)* | Pure computation and existing streams | `strlen`, `memcpy`, `sqrt`, `isalpha`, `snprintf`, `malloc`, `qsort`, `time`, `printf`, `puts`, `exit` |
| `fs` | Files, directories, stdin, memory mapping | `fopen`, `open`, `read`, `glob`, `opendir`, `stat`, `unlink`, `getchar` |
| `exec` | Processes and system state | `system`, `popen`, `fork`, `execv`, `posix_spawn`, `waitpid`, `kill`, `setuid` |
| `net` | Sockets and name lookup | `socket`, `connect`, `getaddrinfo`, `gethostbyname` |
| `env` | Environment and host identity | `getenv`, `setenv`, `uname`, `gethostname`, `getpwnam` |
| `dlopen` | Loading shared libraries | `dlopen`, `dlsym` |
| `thread` | Threads and locks | `pthread_create`, `thrd_create`, `mtx_lock` |
| `signal` | Signals, timers and exit handlers | `sigaction`, `alarm`, `setitimer`, `atexit` |
| `ipc` | System V IPC and shared memory | `shmget`, `msgget`, `semget`, `shm_open` |
| `all` | Every class, plus functions no class lists | |

`--comptime-allow` takes a comma-separated list and can be repeated. An unknown class is an error.

## When a call is denied

Compilation stops at the first denied call and names the function and the flag that allows it:

```
error: comptime call to 'fopen' denied (class fs); pass --comptime-allow=fs
```

A function that no class lists (for example one bound from a shared library with `extern`) is denied until `--comptime-allow=all`.

## Example: reading files at comptime

```c
#include[[cccc::comptime]] <glob.h>

[[cccc::comptime]]
int count_inputs(void) {
    glob_t g;
    glob("data/*.txt", 0, NULL, &g);
    int n = (int)g.gl_pathc;
    globfree(&g);
    return n;
}
```

This needs `cccc --comptime-allow=fs prog.c`. A `[[cccc::comptime]]` attribute, a source comment or a `[[cccc::test(flags=...)]]` string cannot grant access, so a cloned project cannot widen its own permissions.

## Recording grants in a config

A project can list the classes it needs in its `.cccc` instead of repeating them on every command:

```ini
comptime-allow = fs,exec
```

cccc applies the line only after you approve it (`cccc --trust`, or `y` at the prompt), and prints what it grants. A line in your own user config applies without approval. See [CONFIG.md](CONFIG.md).

## Combining with the FFI flags

At comptime a call must pass both the comptime classes and `--ffi-allow` / `--ffi-deny` / `--disable-ffi`. The FFI flags can only narrow what a class grants.

## Limits

The policy is accident prevention, not isolation.[^1] Comptime pointers are real host pointers, so a raw store through a bad address can still crash or corrupt the compiler.

[^1]: The check runs on each native call by name, in `cccc_check_ffi_policy` and the `dlopen`/`dlsym` opcodes. Names live in `src/comptime_policy.c`; a new libc binding needs a row there, which `tests/host/test_comptime_policy.c` enforces.
