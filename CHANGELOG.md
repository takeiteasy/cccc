# Changelog

All notable changes to CCCC are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Version history
before the 0.1.0 reset is not relisted here — see the ticket tracker and
`git log` for the historical record.

## [Unreleased]

- Added: debugger `reload` and `fix` expand calls to an unchanged comptime
  function in a live session, including from `--watch`. Changed or new
  comptime functions and variables and file-scope comptime calls are still
  refused.

## [0.19.4] - 2026-10-22

- Fixed: mutex and condition handles a guest never destroys are freed at
  exit, so threaded programs are clean under valgrind and `leaks`.
- Fixed: comptime `malloc`/`calloc`/`realloc` use the VM heap under `-V`
  (a `strcpy` into the result no longer faults).
- Tests: leak skips lifted from the pthread and comptime tests.

## [0.19.3] - 2026-10-21

- Fixed: the compiler frees its exit-time state (include and framework
  paths, serialize tables, file-scope asm and comptime records, the builder
  root), so a run is clean under valgrind and `leaks`.
- Fixed: assigning an anonymous aggregate to itself no longer overlaps its
  own copy.
- Added: `--test-run` runs `cccc_launch` in the VM smoke phase.
- Tests: `test_leaks` target runs every suite and loose test under `leaks`
  (macOS) or valgrind (Linux); comptime native smoke checks ported to loose
  tests and `tests/smoke` scripts; `test_suite_c23` runs under `-c=native`.

## [0.19.2] - 2026-10-20

- Fixed: threaded programs reclaim `alloca`/VLA storage at block and frame
  exit (reclamation previously stopped once any thread was created). Each
  thread reclaims only its own storage, and a thread ending through
  `pthread_exit`/`thrd_exit` has its outstanding storage reclaimed.
- Fixed: `__builtin_object_size` and `__builtin_dynamic_object_size` types
  1 and 3 bound the answer by the member the pointer names (`p->buf`,
  `&p->in.x`, `&p->buf[i]`, `p->buf + k`), matching gcc/clang; a struct's
  last array member counts as flexible. Array members that decay without
  `&` fold statically.
- Tests: thread-interleaving suite covering `_Thread_local`, `tss` and
  thread identity across `thrd_sleep`, mutex, condition-variable and join
  hand-offs.

## [0.19.1] - 2026-10-19

- Fixed: `<string.h>` declares size arguments and the `strlen`/`strspn`/
  `strcspn`/`strxfrm` returns as `size_t` (was `long`).
- Fixed: `-c=native` supplies the C23 `<math.h>` functions host libms lack
  (`exp10`, the pi-scaled trig family, `fmaximum`/`fminimum` family,
  `totalorder`, `canonicalize`, payload functions, `llogb`, `fromfp` family)
  and `memset_explicit`, `memalignment`, `free_sized` and
  `free_aligned_sized`, running the same code as the VM.
- Fixed: `-c=native` renames block-scope typedefs, tags and enumerators that
  reuse a name visible at the top of their function.
- Fixed: `-c=native` gives a called `gnu_inline` body a weak out-of-line
  definition, and a later non-inline definition replaces an earlier
  `gnu_inline` one.
- Fixed: a program's own definition of a library function (`fabs`) wins over
  the host builtin under `-c=native`.
- Fixed: user macros no longer rewrite `-c=native` output after the include
  replay (`#define int long long`).
- Fixed: `-c=native` finds the host headers when an `-I` names CCCC's include
  directory, no longer replays `#warning`, and passes `-w` to the host
  compiler.

## [0.19.0] - 2026-10-19

- Added: `-c=native` and `-m` enforce `--checked-pointers` (and
  `#pragma cccc config(checked_pointers)`) in the C they emit. A violation
  prints the VM's banner and exits with status 255. See `docs/SAFETY.md`.
- Changed: `-c=native` and `-m` no longer warn that `--checked-pointers` is
  ignored. `-c=generated` still drops it.

## [0.18.0] - 2026-10-19

- Added: `@ctor("prefix")` names the constructor `prefix_T_new`.
- Added: `@default(expr)` on a struct member gives `@ctor`'s `T_new` that value
  instead of a parameter. See `docs/DERIVE.md`.
- Changed: `@generate_getters`, `@generate_setters`, `@generate_accessors` and
  `@generate_constructor` are `@getters`, `@setters`, `@accessors` and `@ctor`;
  the constructor is `T_new` (was `T_create`); `@constructor_skip` is
  `@ctor_skip`. The old names warn as unknown attributes.
- Removed: `@builder`, `@builder_default` and `@builder_required`. `@ctor` with
  `@default` covers them.
- Fixed: a custom attribute named like a C keyword (`@default`) reached its
  handler as an unknown attribute.
- Added: `CopyNode` copies labels, `goto`, `switch`/`case`, local declarations,
  compound assignments, struct-returning calls and checked-pointer accesses.
  Each copy has its own labels and locals. `static` locals and checked-pointer
  declarations are still refused.
- Added: `CopyNode` copies `cleanup` variables and variable-length arrays; each
  copy runs its own handler and has its own size.
- Fixed: a `CopyNode` copy that only reads an enclosing variable-length array
  read an uninitialised size.

## [0.17.2] - 2026-10-18

- Added: `CopyNode(n)` deep-copies an expression or simple statement so a macro
  can link it into a second list.
- Fixed: `Block`, `BlockAddStmt`, `NodeList`, compound-literal and call
  arguments, `Quote` list splices and macro results that are statements reject
  a node another list already holds, instead of rewriting the earlier list.
  See `docs/COMPTIME_POLICY.md`.

## [0.17.1] - 2026-10-18

- Fixed: compiler builtins that link nodes (`Block`, `NodeList`, `BlockAddStmt`,
  `LoopSetBody`, `SwitchAddCase`, `SwitchSetDefault`, compound-literal and call
  arguments, `StructAddField`, `Quote` statement placeholders) reject a node
  that would contain itself or repeat in one list, instead of crashing or
  hanging the compiler. `Block` and call arguments end their `next` chain.
  See `docs/COMPTIME_POLICY.md`.

## [0.17.0] - 2026-10-18

- Changed: compiler builtins check their string, string-array, out-pointer and
  buffer arguments against VM memory, including when called through a function
  pointer. `Quote`/`QuoteLazy` check each `$N` argument is a `Node *` handle
  and that the template gets as many arguments as it references;
  `MacroErrorAt`/`MacroWarningAt` format with the comptime `printf` rules.
  `$N` inside string literals, character literals and comments is not a
  placeholder. See `docs/COMPTIME_POLICY.md`.
- Fixed: a file-scope macro's string arguments are VM memory, so `strlen(name)`
  and `name[0]` work on them.
- Changed: a comptime function that returns through an overwritten saved frame
  pointer or return address is a compile error instead of running on a forged
  frame. See `docs/COMPTIME_POLICY.md`.
- Fixed: `asprintf` no longer crashes; its format string was passed as a
  variadic argument.
- Changed: comptime libc calls check their pointer arguments against VM
  memory, and `free`, `realloc`, `longjmp` and the wide-integer, decimal and
  overflow operations are confined too; bad pointers are compile errors.
  Strings and structs libc returns from host memory (`strdup`, `strerror`,
  `localtime`, ...) are VM copies, so comptime code can read them. `%n` is
  rejected in comptime `printf`. See `docs/COMPTIME_POLICY.md`.
- Fixed: comptime code can read what granted libc calls return: `getenv`,
  `getpwnam`, `getgrnam`, `localeconv`, `gethostby*`, `getservby*`,
  `getprotoby*`, `getcwd`, `realpath` and the `scandir` list are VM copies, and
  `glob`, `readdir`, `fts_read`, `getaddrinfo`, `wordexp`, `hsearch`,
  `tsearch` and `mmap` results are readable until their release call.
- Changed: comptime libc calls follow the pointers stored in their arguments
  (`strtok_r` save pointer, `iconv` buffers, `mbsrtowcs`/`wcsrtombs` source,
  `insque`/`remque` links, `hsearch` key, `tsearch`/`tfind`/`tdelete`/`twalk`
  roots, `getopt`/`getopt_long` argv and long options, `alphasort` entries,
  `regex_t`) and reject NULL callbacks; a bad pointer is a compile error.
  `hsearch` keys are read-only to comptime code. See
  `docs/COMPTIME_POLICY.md`.
- Fixed: a host fault inside a native call during comptime prints one line and
  exits with status 1 instead of risking a hang on a libc lock. See
  `docs/COMPTIME_POLICY.md`.

## [0.16.0] - 2026-10-17

- Added: `--sandbox` confines the process with the OS sandbox (macOS
  `sandbox_init`, Linux landlock + seccomp): no network, writes only to output
  and temp directories, exec only of cccc and the host C toolchain. See
  `docs/SECURITY.md`.
- Changed: URL `#include` and `#embed` download only with `--allow-url`; cached
  copies still resolve.
- Changed: runtime `dlopen` returns NULL under `--disable-ffi`.
- Changed: `CcccExecutable` and `CcccObject` child cccc processes run with
  `--no-config` and inherit the build's `--comptime-allow`, `--allow-url`,
  `--disable-ffi` and `--ffi-allow`/`--ffi-deny`.
- Changed: comptime loads and stores outside VM memory are compile errors
  instead of host crashes. Compiler builtins work under `--disable-ffi` and
  return names and arrays as VM copies. See `docs/COMPTIME_POLICY.md`.

## [0.15.0] - 2026-10-17

- Added: `CcccObject` build target compiles a cccc source to an object file
  (`cccc --compile=object`) that other targets take with `LinkWith`. See
  `docs/BUILD_MODE.md`.

## [0.14.0] - 2026-10-17

- Added: `-c=object` compiles to an object file with the host compiler and
  does not link. See `docs/NATIVE.md`.
- Changed: repeating `-c` with a different format is an error.

## [0.13.0] - 2026-10-17

- Added: config files. A user file and the nearest project `.cccc.toml` set
  default command-line options in TOML; a project file that grants
  capabilities needs approval first. See `docs/CONFIG.md`.
- Added: `--comptime-allow=<class>[,...]` grants comptime code the `fs`,
  `exec`, `net`, `env`, `dlopen`, `thread`, `signal` and `ipc` capability
  classes, or `all`. See `docs/COMPTIME_POLICY.md`.
- Changed: comptime code can call only a safe subset of libc unless a class is
  granted; calls to anything else are denied.
- Changed: taking the address of `setjmp`, `_setjmp` or `sigsetjmp` is a
  compile error in the VM.
- Fixed: a static or global function pointer initialised with `free`
  (`static void (*p)(void *) = free;`, `{0, realloc, free}`) failed with
  `undefined relocation target: free` in the VM and under `-c=native`.
- Fixed: the address of `signal`, `raise`, `longjmp`, `_longjmp`,
  `siglongjmp`, `dlopen`, `dlsym`, `dlclose` or `dlerror` evaluated to NULL in
  the VM, or failed in a static initialiser; it is now callable through a
  function pointer.

## [0.12.0] - 2026-10-17

- Added: `@range(min, max)` on an integer, enum or bit-field member, with
  `@validate` and `@clamp` generating `T_validate` and `T_clamp` for a struct.
  See `docs/DERIVE.md`.
- Added: `@compare_order(N)` and `@compare_desc` choose the order and direction
  `@compare` uses for a member.
- Added: `@equals`, `@hash`, `@dump`, `@compare`, `@to_json` and `@from_json`
  treat `@owned` members by content.
- Added: `@clone` and `@destroy` work through a pointer to a struct that is
  declared but not yet defined.
- Added: `@bitflags` accepts composite aliases such as `RW = R|W`, and member
  attributes can follow a bit-field width.
- Added: `ConstIntValue(node, &out)` reads an integer constant expression
  argument of a macro or attribute. See `docs/MACROS.md`.
- Added: `@serialize_skip`, `@accessor_skip` and `@constructor_skip` leave a
  member out of `@serialize` / `@deserialize`, the getters and setters, and
  `@generate_constructor`.
- Changed: an `@name` attribute with no registered handler warns by default
  (`-Wunknown-attributes`).
- Fixed: `-c=native` output for array-of-array subscripts, widening casts and
  empty structs.
- Added: `@builder` generates a `T_builder` type with chaining setters and
  `T_build` for a struct, with `@builder_default` and `@builder_required`
  members. See `docs/DERIVE.md`.
- Added: `@clone` and `@destroy` generate `T_clone` and `T_destroy` for a struct,
  deep-copying and freeing the pointer members marked `@owned`. They need
  `#include <stdlib.h>`. See `docs/DERIVE.md`.
- Added: `@compare` generates `int T_compare(const T *a, const T *b)` for a
  struct, ordering by members in declaration order. See `docs/DERIVE.md`.
- Added: `@bitflags` on an enum of power-of-two flags generates `T_set`,
  `T_clear`, `T_toggle`, `T_has` and `T_format`. See `docs/DERIVE.md`.
- Added: `@equals`, `@hash` and `@dump` generate `T_equals`, `T_hash` and
  `T_dump` for a struct, with `@equals_skip` and `@dump_skip` to leave members
  out. `@dump` needs `#include <stdio.h>`. See `docs/DERIVE.md`.
- Added: `@to_json` and `@from_json` generate `T_to_json` and `T_from_json` for
  a struct; they need `#include <cccc/json.h>` as well as `<cccc/derive.h>`.
  See `docs/DERIVE.md`.
- Changed: the built-in attributes (`@serialize`, `@deserialize`,
  `@enum_to_string`, `@enum_from_string`, `@generate_*`) need
  `#include <cccc/derive.h>`; without it they are a compile error that names
  the header.
- Fixed: a comptime-generated function can call a `static` function of the
  program.
- Fixed: `MakeCast` on a `MakeMember` or `MakeSubscript` node crashed the
  compiler.

## [0.11.1] - 2026-10-16

- Fixed: `switch (f(x))` with a function call as the controlling expression
  always took the first matching branch in the VM.
- Fixed: `Serialize`, `Deserialize`, `@serialize` and `@deserialize` need no
  particular alignment of the buffer; `Deserialize` rebuilds the value member
  by member instead of casting the buffer.

## [0.11.0] - 2026-10-16

- Fixed: a guest thread that runs between two `--testing` tests no longer leaves
  its stack in the VM, so the next test no longer runs on freed memory (a
  sporadic crash on macOS). Blocking calls such as `usleep` and `wait` now keep
  the calling thread's thread-local storage across the wait.
- Fixed: a guest signal handler that runs right after a call returns no longer
  replaces that call's result with the signal number.
- Fixed: a `[[cccc::comptime]]` macro call is expanded in `case` labels
  (including ranges), enumerator values, `_Static_assert` conditions and
  file-scope array lengths. See `docs/MACROS.md`.
- Fixed: `@serialize`, `@deserialize`, `@enum_to_string`, `@enum_from_string`,
  `@generate_getters`, `@generate_setters` and `@generate_constructor` accept
  `typedef` declarations as well as tagged ones.
- Fixed: `@serialize` copies bit-fields and the fields of anonymous
  struct/union members.
- Fixed: `@generate_getters`, `@generate_setters` and `@generate_constructor`
  handle array members and anonymous struct/union members.
- Fixed: `@enum_to_string` accepts enums with aliased values; the first name
  wins.
- Fixed: `GenerateSum`, `GenerateMap`, `GenerateReduce` and `GenerateFilter` can
  be called more than once for the same element type.
- Added: `@generate_getters("Prefix")` and `@generate_setters("Prefix")` name
  the functions `Prefix_get_<field>` / `Prefix_set_<field>`, and
  `@generate_accessors` publishes both.
- Added: `StringLiteralValue(node)` reads a string literal macro argument.

## [0.10.8] - 2026-10-16

- Fixed: a thread left running by a passing `--testing` test no longer unbalances
  VLA/`alloca` reclamation in later tests. Reclamation stays off for the rest of
  a run once a thread has been created.
- Fixed: a thread created before the program's first `pthread_self`, mutex or
  TSS call is tracked, so it is reaped at exit.
- Fixed: `--testing` stops the threads a failed or timed-out test left behind,
  and a failed `Assert` in a setup hook, forked `exit_code=` child, worker or
  callback no longer crashes, hangs or re-runs later tests. See
  `docs/TEST_MODE.md`.

## [0.10.7] - 2026-10-15

- Fixed: `--testing` no longer hangs after a test fails an `Assert` or times out.
  A later `exit_code=` test or a test that starts a thread now runs normally.

## [0.10.6] - 2026-10-15

- Fixed: a VLA length expression with a side effect runs once under
  `-c=native`/`-m`, as in the VM. A block-scope typedef of a variably modified
  type evaluates its length where it is declared, in the VM too.
- Fixed: `--testing=native` honours `--test`, `--test-suite`, `--fail-fast`,
  `--test-timeout` and `--list-tests`, and the built binary accepts them
  directly. `--test-format=plain|json` is refused with it. See
  `docs/TEST_MODE.md`.

## [0.10.5] - 2026-10-15

- Fixed: `__builtin_fabs`/`sqrt`/`floor`/`ceil`/`trunc`/`round`/`copysign`/`fmin`/`fmax`/`fma`
  (+ `f`/`l`) with constant arguments fold at compile time, so they are valid in
  static initializers, `constexpr` and `_Static_assert`. See `docs/STDLIB.md`.

## [0.10.4] - 2026-10-15

- Changed: a same-scope typedef redefinition with a different type, or a typedef
  of a name already bound as an enumerator or variable, is an error. A clash
  between two system headers warns. See `docs/TYPES.md`.
- Fixed: `-c=native` and `-m` define a tagless struct or union once under a
  `__cccc_anon<N>` tag, so assigning one anonymous-struct lvalue to another
  builds with the host compiler. See `docs/NATIVE.md`.
- Fixed: `-c=native` defines a `static` whose struct type is declared in the
  function, and a tagless global with no initializer, instead of dropping it.
- Fixed: `tests/host/test_watch_cli.c` builds on Linux.

## [0.10.3] - 2026-10-14

- Added: `--link-arg=ARG` passes `ARG` to the host link line under `-c=native`;
  `AddLdFlag` and `AddFramework` on `CcccExecutable` use it. See
  `docs/NATIVE.md` and `docs/BUILD_MODE.md`.
- Added: the `__sync_*` atomic builtins and the `__sincos*_stret` FFI functions.
- Added: unchecked `format` archetypes (`CFString`, `NSString`, `os_trace`, ...)
  are accepted; an unknown archetype warns.
- Fixed: GNU `extern inline` bodies are inline-only definitions.
- Fixed: `__inline`, `__restrict` and `__thread` spellings, and gnu89 `inline`,
  are accepted before C99/C11.
- Fixed: an SDK `Availability.h` no longer taints `<stdio.h>`; only a file
  identical to cccc's own header is treated as its copy.
- Fixed: `-c=native` declares input functions, types and transitive anchors
  ahead of a replayed implementation-style header, and copies multi-line
  directive comments whole.
- Fixed: `-Wparentheses` parenthesizes operand pairs.
- Fixed: a header that ends in a conditional after its include guard's `#endif`
  (the stb two-include layout) is no longer skipped on a second `#include`.

## [0.10.2] - 2026-10-14

- Added: `__builtin_fabs`, `sqrt`, `floor`, `ceil`, `trunc`, `round`,
  `copysign`, `fmin`, `fmax` and `fma` (and their `f`/`l` forms) without
  `<math.h>`. See `docs/STDLIB.md`.
- Added: `_Float16` as a storage-only type: declarations, layout, pointers and
  prototypes work; using a value is an error. See `docs/COVERAGE.md`.
- Changed: `#include_next` and `__has_include_next` continue into framework
  directories.
- Fixed: attributes that SDK headers use (`availability`, `visibility`, ...)
  no longer run the comptime pass on programs without comptime code.
- Added: framework header lookup. `-F`/`--framework-path` and `--iframework`
  resolve `<Name/X.h>` to `Name.framework/Headers/X.h`, and `--sysroot` adds
  `<sdk>/System/Library/Frameworks`. See `docs/HEADERS.md`.
- Changed: `-F` is `--framework-path`; `--disable-ffi` has no short form.
- Fixed: a byte that is not valid UTF-8 inside a comment is no longer a
  compile error.

## [0.10.1] - 2026-10-14

- Added: `--repl --watch` reloads the files `:load` read when they are saved,
  at the prompt without pressing Enter. See `docs/WATCH.md`.
- Changed: `--watch` reloads only the files that include a changed header
  instead of every file of the program, and watches a header a reload starts
  to include.
- Fixed: a long `--watch` session no longer grows the list of input files by a
  duplicate on every reload.
- Release tarballs include `THIRD_PARTY_NOTICES.md` (chibicc, stb_sprintf,
  dmon, libbacktrace).

## [0.10.0] - 2026-10-14

- Added: `--watch` hot-reloads a running program when a source file is saved
  (changed functions are redefined, globals keep their values), restarts it
  when it exits, and waits for the fix after a compile error. With `--build`,
  or when the file carries `[[cccc::build]]`, a save rebuilds. See
  `docs/WATCH.md`.
- Added: `--watch-path`, `--watch-ignore`, `--watch-debounce`, `--watch-clear`
  and `--watch-restart`.
- Added: `Watch`, `WatchGlob`, `IgnoreWatch` and `BuildWatching` in
  `building.h`.
- File watching uses the vendored dmon library (`src/dmon.h`); macOS builds link
  CoreServices.

## [0.9.2] - 2026-10-14

- Fixed: a debugger condition, return-value expression or REPL `:type`
  expression with tokens after the expression (`x == 1 garbage`) is an error
  instead of being read as the part before them.
- Fixed: `arr[i]++`, `arr[i] += k` and the same on a multi-dimensional or
  struct-member local array no longer tag the array under `-3`.
- The `test` build target also runs the host tests (`tests/host/test_*.c`).

## [0.9.1] - 2026-10-14

- Fixed: a debugger condition that names a `__block` local, a variable-length
  array or a struct or union parameter reads its value (a `__block` condition
  never fired).
- Fixed: a debugger condition with a compile error (a non-scalar result, or a
  block that captures an array or `__block` local) prints the error and counts
  as false instead of ending the process.
- Fixed: a debugger condition reads paused-frame locals after a string literal
  and inside statement expressions.
- Fixed: a block literal in a debugger condition captures paused-frame locals
  by value.
- Fixed: `++`, `--` and compound assignment on a local no longer record its
  address under `--dangling-pointers`, which removes a hashmap write per
  update and a stale tag a later frame could hit.
- Fixed: re-taking an address under `-3` no longer leaks its provenance entry.
- Fixed: a block-scope `_Thread_local` without `static` or `extern` is an error,
  as in gcc and clang.
- Fixed: a block-scope `static _Thread_local` variable is private to each
  thread instead of shared.

## [0.9.0] - 2026-10-14

- Fixed: the debugger's `reload` and `fix` print their compile errors (they
  were silent under `-g`).
- Fixed: a `reload` of a file with `[[cccc::comptime]]` functions leaves the
  session's comptime functions in use. It refuses a changed or new comptime
  function or variable, a file-scope comptime call, and a new or changed
  function that calls a comptime function; `fix` refuses such a call with a
  clear message.
- Changed: a debugger condition or REPL expression that only reads program
  state gives back its string literals, block literals and labels too, and
  data freed below newer data is reused by later conditions.
- Fixed: `--dangling-pointers` no longer reports a variable-length array's
  pointer slot as dangling when an earlier call left a stale tag at that
  address.
- Fixed: `[[cleanup]]` written before the type in a `for` initializer
  (`for (__attribute__((cleanup(f))) int v = 0; ...)`) now runs its handler
  when the loop ends.
- Fixed: a `for` initializer accepts any declaration: C23 `auto`, `constexpr`,
  `_Alignas`, `static`, `extern` and `typedef`. Before C23, the last three
  warn under `-Wpedantic`; `_Thread_local` without `static`/`extern` is an
  error. A `const` or `constexpr` initializer is no longer rejected as an
  assignment.
- Added: debugger `return` and `restart` run the `[[cleanup]]` handlers the
  popped frames still owe; `return -n` and `restart -n` skip them.
  `cc_debug_return` and `cc_debug_restart` take a `run_cleanups` argument.
- Fixed: the address a `[[cleanup]]` handler receives is no longer tagged for
  dangling-pointer detection (`-3`), which could report a stale frame epoch.
- Added: the debugger's `reload <file>` and the REPL's `:reload <file>` re-read
  an edited source file and redefine the functions whose bodies changed;
  globals and statics keep their values. Embedders use `cc_debug_reload` and
  `cc_repl_load_file`.
- Fixed: a conditional breakpoint (and `print`, `return <expr>`) no longer
  trips dangling-pointer detection (`-3`) when a returned frame left a
  `&local` tag on the slot being read.
- Added: the REPL redefines functions (hot reload). Direct calls, function
  pointers and host callbacks reach the new body; block-scope statics keep
  their storage. Embedders use `cc_repl_init`, `cc_redefine_function` and
  `cc_repl_eval`.
- Added: debugger frame control. `bt`, `frame`, `up` and `down` select a call
  frame (`print` reads its locals); `return [expr]` pops it as if it returned
  `expr`; `restart` re-enters it from the start with its original arguments
  (explicit `-g`). Embedders use `cc_debug_return` and `cc_debug_restart`.
- Added: debugger `fix <function>` replaces a function while paused (edit and
  continue with `restart`). Embedders use `cc_debug_fix`.
- Fixed: long REPL and debugger sessions no longer grow the VM's text and data
  segments: expression and `return <expr>` wrappers are given back after use,
  and conditional breakpoints with the same condition in one function share
  one compiled wrapper that is released when its breakpoints are deleted.
  Code freed below newer code is reused by later conditions.
- Fixed: the debugger's symbol table grows on demand; `print` and watchpoints
  find globals and locals past the first 4096 symbols.
- Added: `fix <function>` opens `$VISUAL`/`$EDITOR` on the function's current
  source when a terminal is attached, and offers to re-edit on a compile error.
  Macros of the last file expand in `fix` source.
- Fixed: `break file:line` finds code compiled at the REPL or by `fix`;
  breakpoints follow a redefined function.
- Fixed: a conditional breakpoint evaluating to a negative number counted as
  an evaluation error; a parse error in a breakpoint condition no longer
  unwinds into the program's error handler; a REPL expression unit no longer
  renumbers the stack offsets of the function defined before it.
- Fixed: a REPL function defined after an earlier prototype line is compiled;
  every REPL definition after the first is no longer parsed as a nested
  function; a `static inline` function defined at the REPL is compiled.

## [0.8.5] - 2026-10-12

- Added: `--asm-passthru` accepts bit-field output operands (`=r`, `+r`); a
  bit-field with a memory constraint is a compile error.
- Changed: tests run through `build.c` targets. `test` runs the `[[cccc::test]]`
  suites in VM and `-c=native` mode; `test_warnings`, `test_serialize`,
  `test_build_mode`, `test_safety`, `test_parallel`, `test_macros` and
  `test_misc` (all: `test_extra`) run the single-file tests. The Python
  runner (`tools/run_tests.py`, `tools/tests.py`, `tools/testing/`) is gone.

## [0.8.4] - 2026-10-11

- Added: GNU extended `asm` parses with operands, clobbers and `goto` labels
  and is emitted verbatim under `-c=native`/`-m`. The VM leaves outputs
  untouched; `--asm-passthru` passes the operands to one batched host thunk
  build.
- Fixed: rvalue expressions (arithmetic, shifts, `?:`, comma, casts, calls,
  assignment and `++`/`--` results, statement expressions) drop
  `const`/`volatile`/`restrict`/`_Atomic`. Assigning to or taking the address
  of a comma, call or cast result is a compile error.
- Fixed: `-c=native` assigns through a compound literal or one of its members
  (`(int){3} = 4`, `(struct P){1,2}.x = 5`).
- Fixed: `-c=native` emits no `-Wunused-value` noise for a discarded
  `_Atomic` `++`/`--`/`op=` or a discarded comma operand.
- Fixed: postfix `++`/`--` on an `_Atomic` operand yields the CAS loop's
  old value, and `int a = ++ai;` on an `_Atomic int` builds under Apple clang.
- Fixed: an initialised flexible-array-member global keeps its declared
  struct type in expressions (`sizeof`, `typeof`, `&g + 1`, struct copy); a
  non-empty flexible initializer on an automatic object is a compile error.
- Fixed: a `static _Thread_local` global is declared once under gcc.
- Fixed: a diagnostic caret mirrors tabs on tab-indented source lines.
- Changed: libbacktrace is a git submodule (`vendor/libbacktrace`); an
  uninitialised submodule builds without it.

## [0.8.3] - 2026-10-10

- Fixed: casting an unsigned (or `_Bool`/pointer) scalar to a `_BitInt` wider
  than 64 bits zero-extends in the VM; it used to sign-extend, so
  `(_BitInt(256))0xffffffffffffffffull` came out as -1.
- Fixed: unary `+`, `~`, `<<` and `>>` promote a `char`/`short`/`_Bool`/enum
  operand to `int`, so `sizeof(~c)` is 4 as in gcc and clang.
- Added: `-c=native`/`-m` lower anonymous struct/union members, flexible array
  members and `&(struct S){...}.member` in a struct or union with a
  `_BitInt(N>128)` bit-field, and a global initializer keeps the pointers
  inside such a struct. A pointer in a union or at a `packed` offset is still
  refused.

## [0.8.2] - 2026-10-10

- Added: `-c=native`/`-m` lower a struct or union with a bit-field whose
  declared type is a `_BitInt` wider than 128 bits, by emitting it as opaque
  bytes in CCCC's own layout. Anonymous and flexible array members in such a
  struct, and pointers in its global initializer, are still refused.
- Fixed: `_Alignof(expr)` and `__alignof__(expr)` report a typedef's
  `aligned(N)` when the expression's type is that typedef (`*p`, `p[1]`,
  `-g`, `g + g`), and the plain type's alignment when the result is another
  type (`g + 1`). Under `--compiler-family=clang` a cast keeps the typedef's
  alignment too.
- Added: `-mavx`, `-mavx2` and `-mavx512f` on x86_64. They define `__AVX__`,
  `__AVX2__` and `__AVX512F__`, raise gcc's cap on C11 `_Alignof(type)` of a
  wide vector to 32 or 64, and are forwarded to `-c=native`'s compiler.
- Fixed: `_Alignof(type)` and `__alignof__(type)` of a typedef with
  `aligned(N)` report `N` instead of the underlying type's alignment.

- Fixed: on Linux x86_64 under `--compiler-family=gcc`, C11 `_Alignof(type)`
  and `_Alignas(type)` of a 32- or 64-byte vector (or a struct, union or array
  holding one) report gcc's 16 instead of the vector's size. `__alignof__`,
  `_Alignof expr` and `-c=native` constants are unchanged.
- Added: `__alignof__` and `__alignof` are keywords available before C11.

## [0.8.1] - 2026-10-06

- Added: under `-2` and `-3`, a subscript directly over a fixed-size local or
  global array (`buf[k]`, `m[i][j]`, `s.arr[k]`) is bounds-checked at the
  access. `a[size]` and negative indices are reported; `&a[size]` and
  `a + size` stay legal. A struct's trailing array is treated as flexible.
- Fixed: `-x cl -m`/`-c=native` run outside the repo (no `-I`) no longer drops
  the OpenCL typedefs (`float4`, `uint`, ...) from the output.
- Fixed: `-c=native` no longer fails the layout assertion for a struct holding a
  32- or 64-byte vector under x86_64 gcc.
- Fixed: `p++`, `p--` and `p[-1]` on a pointer into stack or global memory no
  longer report "Negative array index" under `-2` and `-3`.
- Fixed: `--dangling-pointers` no longer reports a live frame's own array
  access (`buf[i]`, `m[i][j]`, `s.arr[i]`, including `+=` and `++`) as
  dangling when a returned frame's local once occupied that address.
- Fixed: locals and parameters with alignment above 8 (`_Alignas(N)`,
  `__int128`, wide `_BitInt`, wide vectors) have correctly aligned addresses
  in the VM, and `--dangling-pointers` tracks the aligned address.
- Fixed: `vector_size` types are aligned as the host ABI aligns them (capped
  at 16 on aarch64 and macOS), so locals, struct layout and `-c=native`
  agree.

## [0.8.0] - 2026-10-05

- Added: GNU `__auto_type` declares a variable typed from its initializer.
- Added: `-Wauto-declarator`, part of `-Wall`, flags an `auto` or `__auto_type`
  declaration that gcc rejects (a pointer declarator such as `auto *p = &x;`,
  or several declarators) under `--compiler-family=gcc`, the default. cccc
  accepts the declarations.
- Fixed: a declarator attribute after a C23 `auto` name
  (`auto t __attribute__((unused)) = a;`) is accepted.
- Fixed: a `cleanup` attribute on a variable-length array runs at scope exit
  and receives a pointer to the array, in the VM and under `-c=native`. A
  pointer-to-VLA local with both a `cleanup` and an initializer compiles under
  `-c=native` instead of failing with "redefinition".
- Fixed: the `cleanup` function of a `__block` variable receives the
  variable's own address in the VM.
- Fixed: a VLA declared in a `for` initializer (`for (int n = 3, v[n], i = 0;
  ...)`) compiles under `-c=native`, `-m` and `-c=generated` instead of
  failing with "cannot be serialized to C".
- Fixed: `-Wattributes` warns that `cleanup` does not apply to types in a cast,
  `sizeof` or `__typeof__` type-name, as gcc does.
- Fixed: `__alignof__(v)` and `_Alignof(v)` on a variable or struct member
  report its declared `aligned(N)` / `_Alignas(N)` alignment, not just its
  type's.
- Fixed: `-Wtautological-compare` no longer reports a self-comparison for
  operands that fold to a constant, such as `sizeof(struct P) == 8`.

- Fixed: subscripting a vector rvalue (`(a + b)[0]`, `f()[3]`) is accepted
  instead of failing with "not an lvalue". Assigning to such a lane is still
  an error.

- Fixed: `vector_size` after a pointer, array or function declarator
  (`int *q VS;`, `int b[2] VS;`, `int f(void) VS;`) vectorizes the innermost
  scalar, as in gcc. It was rejected as not a scalar type.

- Fixed: `vector_size` written before the type or after the type specifier
  (`[[gnu::vector_size(16)]] int v;`, `int __attribute__((vector_size(16))) v;`)
  now makes a vector, as in gcc. It was a parse error in declarations, and
  silently ignored in cast, compound-literal and `sizeof` type-names.

- Fixed: compile time no longer grows quadratically with the number of
  globals. A 54,000-line file with 6,000 functions compiles in 0.5 s instead
  of 5.5 s.
- Fixed: computing token columns no longer costs about 0.2 s on every compile
  that uses comptime.
- Fixed: GNU attributes that `__has_attribute` reports as recognized, such as
  `cold`, `hot`, `noinline` and `always_inline`, are accepted silently instead
  of warning "unknown attribute" under `-Wattributes`.
- Fixed: an unknown attribute after a function declarator warns once instead
  of three times.
- Fixed: an attribute after a parenthesised declarator, as in
  `int (*fp)(void) __attribute__((aligned(16)))`, is accepted instead of
  failing with "expected ','". Function attributes such as `format` and
  `nonnull` on a function pointer apply to calls through it.
- Fixed: `[[__gnu__::name]]` is accepted as `[[gnu::name]]`, reserved
  `__name__` spellings work inside `[[...]]`, and `__has_c_attribute(gnu::x)`
  reports recognized GNU attributes.
- Fixed: `-c=native` runs `cleanup` attributes instead of dropping them.
- Fixed: a `break`, `continue`, `return` or `goto` that leaves a scope before a
  cleanup variable's declaration no longer runs that variable's cleanup.
- Fixed: cleanup variables declared in a `for` header or inside a statement
  expression `({ ... })` are cleaned up.
- Fixed: a `cleanup` attribute applies only to the automatic variable that
  names it. A declaration typed from that variable (`__typeof__(v) t`) no
  longer inherits it.
- Fixed: a `cleanup` attribute at the start of a declaration
  (`__attribute__((cleanup(f))) int a, b;`, `[[gnu::cleanup(f)]] int a;`,
  `int __attribute__((cleanup(f))) *p;`) applies to every declarator instead
  of being dropped, and `int a, __attribute__((cleanup(f))) *p;` applies it to
  `p`.
- Changed: as in GCC, `cleanup` is ignored on a typedef, struct member,
  parameter, function, global or `static` local, with a `-Wattributes`
  warning. Static locals and variables of such a typedef no longer run it. A
  C23 `[[gnu::cleanup(f)]]` after the type specifier is ignored.
- Added: `--vm-profile` reports comptime execution in its own section, and
  works with `-c=native`, `-c=generated` and `-m`. With `--json`, the comptime
  profile is a nested `"comptime"` object.
- Fixed: `--vm-profile` no longer drops `main`'s counts when the program
  registers an `atexit` handler. The report no longer has a `cycles` line or
  JSON key; it counted only the last VM entry, so use `total_opcodes`.

## [0.7.3] - 2026-10-02

- Fixed: a `#pragma omp` inside a function body in an emit block no longer
  fails to compile. It now builds an OpenMP region, as it would in ordinary
  source, and is still kept in the emitted output.
- Fixed: `-Wunused` no longer reports a function label as unused when its only
  `goto` is spliced in from a `Quote()`/`QuoteLazy()` template.

## [0.7.2] - 2026-10-01

- Fixed: `-c=generated` no longer copies `#pragma omp` lines, `#include <omp.h>`
  or its guard into the output. A region in macro-generated code runs on the
  thread pool, and the runtime is emitted only when generated code needs it.
  `-m` and `-c=native` no longer replay `#pragma omp` at file scope.
- Added: `_Pragma("omp ...")` in a `Quote()` template builds an OpenMP region.
- Fixed: `-Wlogical-op` and `-Wtautological-compare` no longer fire inside
  `#if`/`#elif` expressions, such as those in the bundled `<stddef.h>`.

## [0.7.1] - 2026-10-01

- Fixed: a call to a single-return `static inline` function evaluated its
  arguments more than once, could report a spurious "not an lvalue", and could
  clobber the caller's locals. Such calls are now ordinary calls.

## [0.7.0] - 2026-10-01

- Added: OpenCL C 1.2 kernels. A `.cl` file, or `-x cl`, is read as OpenCL C:
  `__kernel`/`__global`/`__local`/`__constant`/`__private`, the `get_*_id` and
  size functions, `barrier`, `mem_fence`, the 1.2 atomics (including
  `atomic_xchg` on `float`), vector types of 16, 32 and 64 bytes with
  swizzles and vector literals, and `-cl-std=CL1.2`. See `man/OPENCL.md`.
- Added: `<cccc/kernel.h>` and `<cccc/opencl.h>` are embedded in the compiler,
  so they resolve from any directory without `-I`.
- Fixed: `auto`, `typeof_unqual`, `__atomic_fetch_*` and `_Atomic` compound
  assignment on a `[[cccc::global]]`, `[[cccc::local]]` or
  `[[cccc::constant]]` object gave the compiler's temporary that address space,
  and kernel code rejected it as "a global-memory object on the stack".
- Limitation: no vectors under 16 bytes (`float2`, `int2`, ...), no 128-byte
  vectors, no OpenCL built-in function library, no `half`.

## [0.6.0] - 2026-10-01

- Added: `-c=native` and `-m` run `cccc_launch`, the work-item builtins,
  barriers and `[[cccc::local]]` memory on CPU threads. Kernels without a
  barrier run their work-groups in parallel on the thread pool; kernels with a
  barrier run a thread per work-item. See `man/KERNELS.md`.
- Fixed: `-m` emitted a call to an undefined `__cccc_launch` for a kernel
  launch.
- Limitation: `--test-run` and `-c=generated` do not support `cccc_launch`; a
  native barrier kernel starts a thread per work-item for every group.

## [0.5.0] - 2026-10-01

- Added: `-fopenmp` parses `#pragma omp` (`parallel`, `for`, `parallel for`,
  `critical`, `atomic`, `barrier`, `single`, `master`, with `private`,
  `firstprivate`, `shared` and `reduction`) and the VM runs each region on one
  thread. `<omp.h>` provides the runtime functions. See `man/OPENMP.md`.
- Added: `lastprivate` on `for` and `parallel for`, and `copyprivate` on
  `single`.
- Added: `-c=native` and `-m` run OpenMP regions on real threads from a thread
  pool emitted into the output; `num_threads`, `if`, `critical`, `atomic`,
  `barrier`, `single`, `reduction` and the `omp_*` runtime work as in OpenMP.
- Fixed: the compare-and-swap loop behind `__atomic_fetch_<op>` started with a
  plain read that raced with other threads' updates.
- Limitation: native OpenMP has no `ordered`, `dynamic`/`guided` schedules,
  pointer loop variables, `atomic capture` blocks or Windows support;
  `copyin`, `linear` and `threadprivate` are not supported.

## [0.4.1] - 2026-10-01

- Fixed: the macOS release job failed on flaky tests. The SIGCHLD siginfo and
  aio submission tests in `test_suite_posix.c` are skipped on macOS.

## [0.4.0] - 2026-10-01

- Added: `<cccc/kernel.h>` runs `[[cccc::kernel]]` functions over a grid of
  work-items in the VM. `cccc_launch(kernel, CCCC_RANGE(...), CCCC_RANGE(...),
  args...)` takes a kernel function or function pointer, and checks the
  arguments like a call. See `man/KERNELS.md`.
- Added: work-item builtins (`cccc_global_id`, `cccc_local_id`,
  `cccc_group_id`, the size queries, `cccc_work_dim`) and `cccc_barrier`.
- Added: `[[cccc::local]]` objects in a kernel body and `CCCC_LOCAL(bytes)`
  parameters live in per-work-group memory, under the VM's bounds checks.
- Added: `--kernel-max-group-size=N` (default 256).
- Fixed: a thread that faulted was not reported by the thread runner.
- Limitation: `-c=native` rejects the launch and builtins; kernel arguments
  must be integers, pointers or floats (up to 8 of each kind).

## [0.3.2] - 2026-10-01

- Fixed: `--sysroot` and `--use-system-headers` failed with "expected ','" on
  glibc 2.43 and newer.
- Fixed: `BuildRoot()` returned a dangling pointer, so `DirExists` and
  `FileExists` on it failed intermittently.

## [0.3.1] - 2026-09-30

- Fixed: `defined(__has_c_attribute)`, `#ifdef __has_builtin` and the same
  test for every other `__has_*` operator were false. The operators are now
  visible to `defined`, `#ifdef` and `#ifndef`, and evaluate outside `#if`
  as well (`int n = __has_builtin(__builtin_expect);`).
- Fixed: `#include_next` failed with "cannot open file" after the including
  header had itself included another header. `#include_next` and
  `__has_include_next` now search from the directory after the one the
  current file was found in; `__has_include_next` no longer always returns
  `0`.
- Docs: the `[[cccc::kernel]]` guard in `man/ATTRIBUTES.md` uses a nested
  `#if defined(__has_c_attribute)`, which is valid in C++ and Metal too.

## [0.3.0] - 2026-09-30

- Checked arrays: `int a _Checked[10]` / `char s _Nt_checked[11]` make the
  declared extent part of the array's checked type, so indexing is
  bounds-checked against it and decay to an unchecked pointer carries the
  bounds along. Multidimensional and variable-length checked arrays are
  compile errors. See `man/SAFETY.md`'s "Checked Arrays" section.
- Bounds-safe interfaces: a call passing a checked pointer to a parameter
  with declared bounds is verified at the call site (`CHKAB`) that the
  argument's own bounds imply the parameter's. An annotated parameter needs
  no extra attribute and places no restriction on an unchecked caller.
- Fixed: the VM loaded a clobbered register when an operand of
  `atomic_store*`, `atomic_exchange`, `atomic_compare_exchange_*` or
  `ckd_add/sub/mul` contained a call (a store stored the byte offset; the
  exchange and compare-exchange forms crashed).
- Fixed: an assignment stored a wrong value when its lvalue's address
  expression contained a block call, a wide `_BitInt` operation or a
  statement-expression.
- Fixed: `-c=native` emitted an invalid comparison between an `_Atomic`
  scalar load and a constant (`(_Atomic unsigned)7`).


- Checked-pointer bounds casts: `[[cccc::assume]]`/`[[cccc::dynamic]]`, the
  entry point for converting an unchecked pointer into a checked one.
  Attach in a cast's type-name alongside the claimed checked kind/bounds
  form (cast-only — a compile error on a declarator). `assume` takes the
  claim on trust with no runtime check; `dynamic` verifies it against the
  source's own declared-checked bounds via the existing `CHKAB` opcode,
  with a compile error when the source has no verifiable bounds. Both
  desugar to a compiler-generated declared-checked local, so every existing
  checked-pointer pass (`CHKR`, propagation, `CHKAB`) sees an ordinary
  checked variable with no new resolution machinery. The sanctioned
  unchecked→checked conversion inside a checked region, exempt from its
  cast ban. Also new: `dynamic_check(cond)` /
  `__builtin_cccc_dynamic_check(cond)` (with `_Dynamic_check(cond)` as a
  Checked-C-compat alias, recognized only when no symbol of that name is in
  scope) — a programmer bounds assertion that traps via the new `CHKDC`
  opcode when `cond` is false, under `--checked-pointers`; a no-op
  otherwise. See `man/SAFETY.md`'s "Checked Pointers" § "Bounds casts" and
  § "`dynamic_check`".
- Checked regions: `[[cccc::checked]]`/`[[cccc::unchecked]]` on a function
  definition or compound statement, `#pragma cccc checked/unchecked
  begin/end`, and the Checked C keyword spellings `_Checked { ... }` /
  `_Unchecked { ... }` (recognized positionally, not as reserved keywords, so
  an existing identifier of either spelling is unaffected), require every
  pointer declared within their extent to be a checked kind
  (`single`/`array`/`ntarray`) and forbid casting to or between unchecked
  pointer types there — the incremental-migration half of the
  checked-pointer layer. Always-on compile-time diagnostics, independent of
  `--checked-pointers`; see `man/SAFETY.md`'s "Checked Regions" section.

## [0.2.0] - 2026-09-11

- Inline build recipes and tests alongside `main()`: a single `.c` file can
  now carry `main()`, `[[cccc::test]]` functions, and a
  `[[cccc::build]]`/`[[cccc::build_target]]` recipe together, each invoked
  by its own flag (`cccc`, `cccc --testing`, `cccc --build`). `--build` no
  longer rejects a script for defining `main()`; a file with
  `[[cccc::build_target]]` factories and no entry now runs all of them
  automatically under `--build`.
- New `--no-emit-tests` flag: drops `[[cccc::test]]` bodies from
  `-c=native`/`-c=generated` output entirely, instead of emitting them
  inert.
- `RunCustom` build-script shell: builtins now honour redirects and pipes,
  the last same-direction redirect wins, pipe file descriptors are
  `CLOEXEC`, multi-redirect and `$(...)` command substitution work, and a
  pipeline no longer leaks write-end file descriptors or deadlocks.
- `#if`/`#ifdef` `defined()`/`__has_*` operators are now recognized when
  revealed by macro expansion, not only in the raw source text; an
  unrecognized `__has_*` operator now gets its own diagnostic.
- `--sysroot` now resolves the correct multiarch include directory, and
  `asm-label`/attribute ordering under `--sysroot` is fixed.
- `sys/statvfs.h` gets a real-header hand-off (closing out the
  self-hosting type-shadowing audit).
- A URL `#include` now mirrors nested project-header includes it reaches,
  and its default cache directory is resolved before `--url-cache-clear`
  checks for one.

## [0.1.0] - 2026-09-06

- Initial release.
