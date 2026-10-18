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

A function that no class lists (for example one bound from a shared library with `extern`, or an `asm` statement under `--asm-passthru`) is denied until `--comptime-allow=all`.

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

A project can list the classes it needs in its `.cccc.toml` instead of repeating them on every command:

```toml
comptime-allow = ["fs", "exec"]
```

cccc applies the line only after you approve it (`cccc --trust`, or `y` at the prompt), and prints what it grants. A line in your own user config applies without approval. See [CONFIG.md](CONFIG.md).

## Combining with the FFI flags

At comptime a call must pass both the comptime classes and `--ffi-allow` / `--ffi-deny` / `--disable-ffi`. The FFI flags can only narrow what a class grants.

## Limits

The policy is accident prevention, not isolation.[^1] Comptime pointers are real host pointers, but comptime loads and stores are confined to VM memory (data, stack, heap and thread-local storage). An access outside it is a compile error at the macro call:

```
error: compile-time macro 'poke' aborted (opcode 'STR_W' faulted)
Write of 4 byte(s) at 0x8 is outside VM memory
```

Safe-class libc calls get the same check on their pointer arguments:

```
error: compile-time macro 'poke' aborted (opcode 'CALLF' faulted)
call to 'memcpy' argument 0: Write of 4 byte(s) at 0x8 is outside VM memory
```

| Argument | Checked as |
|---|---|
| Buffer (`memcpy`, `qsort`, `fread`-style) | The bytes the call touches lie inside VM memory |
| String (`strlen`, `strcmp`, `puts`) | A NUL inside VM memory |
| Format string (`printf`, `sscanf`) | The `%s` and `%n`-style arguments it consumes; `%n` and `%1$d` are rejected |
| `FILE *`, `locale_t`, `iconv_t` | A handle libc returned earlier in the same compile |
| `free`, `realloc` | A VM-heap pointer or a host allocation libc returned |
| Callback (`qsort`, `bsearch`, `tsearch`, `lfind`) | Not NULL |
| `longjmp` | A `jmp_buf` saved by `setjmp`: its stack and code positions must still be valid |

Pointers stored inside a checked buffer are followed when libc dereferences them, and a bad one is reported with its byte offset:

```
call to 'iconv' argument 1 [0]: Read of 2 byte(s) at 0x8 is outside VM memory
```

| Call | Followed |
|---|---|
| `strtok`, `strtok_r` | The string being split must be writable; `*saveptr` when the string argument is NULL |
| `iconv` | `*inbuf` and `*outbuf` for their `*inbytesleft` and `*outbytesleft` |
| `mbsrtowcs`, `wcsrtombs` | `*src` must be NUL-terminated, even when `len` bounds the conversion |
| `insque`, `remque` | The `next` and `prev` links of the neighbours |
| `hsearch` | `ENTRY.key` of the entry passed in |
| `tsearch`, `tfind`, `tdelete` | `*rootp` is NULL or a node libc returned for that root variable |
| `twalk` | The root is NULL or a node libc returned |
| `getopt`, `getopt_long` | Every `argv[i]` below `argc`, and each `longopts` entry up to its terminator (`name`, `flag`) |
| `alphasort` | The `d_name` of both entries |
| `regexec`, `regfree` | A `regex_t` filled by `regcomp` and unchanged since |

Copying a root variable or a `regex_t` to another address, or editing it, is a compile error; `ENTRY.data` stays writable.

A function that returns must find its saved frame pointer and return address as it entered. Comptime code that overwrites them through a local's address is a compile error at the return:

```
error: compile-time macro 'probe' aborted (opcode 'LEV3' faulted)
return through a corrupted frame
```

Comptime code can read what libc returns, whichever way libc owns it:

| Result | Comptime sees | Calls |
|---|---|---|
| libc's static buffer | A VM-heap copy | `getenv`, `getpwnam`, `getgrnam`, `localeconv`, `gethostbyname`, `getservbyname`, `strerror`, `localtime`, `ttyname`, `getlogin` |
| A buffer you free | A VM-heap copy, freed with `free` as usual | `strdup`, `scandir` list, `getcwd(NULL, 0)`, `realpath(path, NULL)` |
| Memory libc keeps | The host memory, readable until its release call | `glob` (until `globfree`), `readdir` (`closedir`), `fts_read` (`fts_close`), `getaddrinfo` (`freeaddrinfo`), `wordexp` (`wordfree`), `tfind`/`tsearch` nodes, `mmap` (`munmap`) |

Host memory libc keeps is read-only, except an `hsearch` entry and a writable `mmap`. After its release call a read is a compile error, not a crash. An `FTSENT` is readable until the next `fts_read`, and `tdelete` ends the nodes of that tree. Compiler builtins (`__builtin_*`, `MakeFunction`, ...) stay callable under `--disable-ffi`; they return names and arrays as VM copies.

A fault the checks miss (a native `SIGSEGV`, `SIGBUS`, `SIGFPE`, `SIGILL` or `SIGABRT` inside a libc call) is also a compile error, with or without a terminal. cccc prints a host backtrace first:

```
error: compile-time macro execution terminated by host signal 6
```

A fault inside a native call (`abort`, a libc routine) ends the compile at once, because libc may still hold a lock. The message names the call and carries no error count, and cccc exits with status 1:

```
prog.c:12: error: compile-time execution terminated by host signal 6 in 'abort'
```

Builtins check the opaque handles they take (`Node *`, `Type *`, `Member *`, `Obj *`, `Token *`, `EnumConstant *`, `AttrTarget *`, and arrays of them). A handle must be one the compiler gave comptime code, and of the kind the parameter declares; `NULL` passes:

```
error: compile-time macro 'probe' aborted (opcode 'CALLF' faulted)
call to '__builtin_ast_type_kind' argument 0: not a Type handle
```

Libc calls you grant with `--comptime-allow`, strings and buffers passed to builtins, the extra arguments of `Quote`-style variadic builtins, and pointers stored inside the buffers of calls not listed above are still trusted, so compile untrusted code with `--sandbox`. See [SECURITY.md](SECURITY.md).

[^1]: The check runs on each native call by name, in `cccc_check_ffi_policy` and the `dlopen`/`dlsym` opcodes. Names live in `src/comptime_policy.c`, and each safe name's pointer arguments in `src/comptime_args.c`; a new libc binding needs a row in both, which `tests/host/test_comptime_policy.c` enforces.
