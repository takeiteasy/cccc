# Changelog

All notable changes to CCCC are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Version history
before the 0.1.0 reset is not relisted here — see the ticket tracker and
`git log` for the historical record.

## [0.1.0] - Unreleased

- Initial release.
- Fixed: CCCC's `#pragma once` (and `#ifndef` include-guard) suppression was
  keyed on the raw resolved path *string*, so one physical header reached
  under two spellings in a single translation unit — e.g. `"./internal.h"`
  and, via another header's own `#include "./internal.h"` resolved against
  *its* directory, `"src/././internal.h"` — was included twice and every
  `static inline` helper it defined was reported as a redefinition. The key
  is now canonicalized with `realpath()` on every lookup and insert, falling
  back to the literal string for synthetic/embedded paths. A real host `cc`
  never hit this because its `#pragma once` is inode-based. Surfaced by the
  self-hosting spike (`src/macros.c` includes both `"./internal.h"` and
  `"./parse_internal.h"`).
- Added: `<stdio.h>` now declares `fileno`, and `<signal.h>` now declares
  `sigprocmask` (with `SIG_BLOCK`/`SIG_UNBLOCK`/`SIG_SETMASK`) and gives
  `siginfo_t` its `si_addr` member. All three are used by
  `src/host_signal.c` and were missing from the bundled headers. `fileno`
  is a plain passthrough; `sigprocmask` is a translating wrapper (the guest
  `sigset_t` is CCCC's own 4-byte bitmask, not the host's real `sigset_t`),
  registered for VM/bytecode execution too. `si_addr` overlaps the SIGCHLD
  union arm at its real per-platform offset (24 on macOS, 16 on glibc),
  guarded by `_Static_assert`. Surfaced by the self-hosting spike.
- Added: the bundled `<pthread.h>` now declares `pthread_once`,
  `pthread_once_t`, and `PTHREAD_ONCE_INIT` (the type mirrors the real host
  layout per platform — a 16-byte struct on macOS, `int` on glibc — so a
  serialized `pthread_once_t x = PTHREAD_ONCE_INIT;` stays valid against the
  replayed real header). `pthread_once` is now also FFI-registered for
  VM/bytecode execution: `wrap_pthread_once` runs the one-shot itself with a
  compare-exchange on the control object's state word (the first 4 bytes of
  `__opaque` on macOS, the whole `int` on glibc) and a guest callback for
  the winner. POSIX's "concurrent callers block until the initializer
  completes" holds by construction under the VM's GIL rather than by a
  spin-wait; under `-c=native` the replayed real `<pthread.h>` binds it to
  the host `pthread_once` directly, so no shim is emitted. Surfaced by the
  self-hosting spike (`src/tokenize.c` / `src/parse_init.c`).
- Added: bundled-header POSIX coverage for a batch of symbols CCCC's own
  source uses that resolved against the real system headers under a plain
  `make` build but were missing from the bundled `include/` copy —
  `strtok_r` and `strsignal` (`<string.h>`), `realpath` (`<stdlib.h>`),
  `ctime_r` / `asctime_r` (`<time.h>`), `open_memstream` (`<stdio.h>`), and
  `PATH_MAX` (`<limits.h>`, defined 4096 to cover both hosts — macOS 1024,
  glibc 4096 — with `<sys/param.h>`'s `MAXPATHLEN` now deriving from it).
  All are registered for VM/bytecode execution too (raw host passthroughs).
  `src/main.c` also gains `#include <strings.h>` — it called `strcasecmp`
  relying on a `<string.h>` leak. Surfaced by the self-hosting spike.
- Fixed: `strtok` was declared in the bundled `<string.h>` but never
  FFI-registered, so guest code that included the header and called it
  compiled cleanly and then failed at run time with an undefined-function
  error. Now registered.
- Added: `sigsetjmp`/`siglongjmp`/`sigjmp_buf` support. On the VM they alias
  the plain `setjmp`/`longjmp` opcodes — the VM has no signal-mask concept,
  so the `savemask` argument is evaluated for its side effects then
  discarded. Under `-c=native` they lower to the real host `sigsetjmp`/
  `siglongjmp` (on glibc: `__sigsetjmp`, via a generated `#if defined(__linux__)`
  declaration block) so the signal-mask save/restore that `src/host_signal.c`'s
  crash-recovery guard depends on is genuine. `sigjmp_buf` is the same padded
  `long long[40]` shape as `jmp_buf`. Surfaced by the self-hosting spike —
  `src/host_signal.c` is the first cccc source to ask cccc's own frontend to
  parse this construct.
- Fixed: the synthesized `_setjmp` declaration emitted into `-c=native`/`-m`
  output now carries `__attribute__((returns_twice))`, so the host `cc` does
  not miscompile non-volatile locals kept live across a native `setjmp`
  (`src/host_signal.c` reads such locals after the `siglongjmp` return).
- Fixed: a `setjmp`/`longjmp` (or `sigsetjmp`/`siglongjmp`) argument
  containing a function call — e.g. `longjmp(env, cleanup())` — could clobber
  the register holding the `jmp_buf` address mid-sequence on the VM, causing
  a segfault on the subsequent jump. The env address and the value/savemask
  operand are now evaluated into temporaries in a clobber-safe order.
- Fixed: `src/stdlib/ctype.c`'s own `#include <xlocale.h>` (macOS FFI
  registration for the `_l`-suffixed locale-explicit functions) had no
  bundled counterpart, so it fell through to the real SDK's own
  `<xlocale.h>` -- whose `struct lconv` (via `<_locale.h>`) collided with
  CCCC's own trimmed `struct lconv` (`<locale.h>`) in the same translation
  unit. Only reproduced once cccc's own source was compiled under
  `-c=native`/`-c=generated`, surfaced by the self-hosting spike. Added a
  bundled `<xlocale.h>` (a plain alias for `<locale.h>` + `<ctype.h>`,
  portable on every host) and a matching non-Darwin replay filter so a
  captured `#include <xlocale.h>` is dropped rather than replayed to a host
  where glibc removed the header entirely.
- Fixed: `cccc -c=generated` replayed a vacuous `#ifndef X` / `#endif` shell
  into the output when the guard's only `#define` had been pre-empted by a
  command-line `-D`. A conditional whose entire body was resolved away by
  cccc's own preprocessor — nested shells included — is now dropped from the
  generated output; a conditional with surviving content is still replayed
  verbatim.
- Fixed: `cccc -c=generated` on a translation unit that published nothing
  from comptime and defined no ordinary function printed `failed to link
  programs`, wrote no file, and exited 0. Such a unit now serializes to a
  valid near-empty file. Without `-c=generated` the same input reports
  `input defines no functions or variables` and exits non-zero.
- Fixed: comptime `GetType()` returned `NULL` for any multi-word base-type
  spelling (`"unsigned char"`, `"long long"`, `"unsigned int"`), which then
  flowed silently through `MakeArray`/`MakeConst`/`GlobalVar` into malformed
  generated C. `GetType()` now resolves every standard base-type spelling
  regardless of word order, and an unresolved name is a hard comptime error
  naming the string — `FindType`/`TypeExists` remain the `NULL`-returning
  probes.
- Fixed: `cccc -c=generated` emitted a header twice when a single input
  `#include`d it twice, because the emit-event replay that feeds generated
  output did not dedup identical `#include` lines the way the `-c=native`
  replay does. Identical includes in the unconditional leading run are now
  collapsed.
- Fixed: cross-file `[[cccc::comptime]]` body forwarding pulled in a
  definition for *every* bodyless prototype in the comptime program, not
  only the ones comptime code calls. A `#include @shared` header that also
  declared a module's runtime-only functions made cccc try to compile those
  bodies in the isolated comptime context; the ones that could not
  (file-static state, `<stdio.h>`) left a malformed AST that crashed
  `-c=native`/`-c=generated` code generation. Forwarding is now
  demand-driven — a prototype nothing references is left alone.
- Fixed: `cccc -c=generated`, `-E`, and `--ffi-decls` exited 0 after
  printing an error when the output file could not be opened; `-c=native`
  went on to invoke the host compiler after a serialization error instead
  of failing. All now exit non-zero.
- Fixed: `<string.h>` and `<locale.h>` did not define `NULL`, which C
  requires of both. `#include <string.h>` followed by a bare `NULL` failed
  with "undefined variable 'NULL'".
- Fixed: the guard that stops a `Quote()` template from silently resolving a
  comptime program's own spliced copy of a same-named runtime global also
  rejected a declaration-only `extern` brought in with `#include @shared`,
  which has no comptime-side storage to shadow and is legitimately named from
  a template. A generated program can again refer to a shared runtime
  constant by name.
- Added: `[[cccc::comptime]]` code can call an ordinary C function whose
  definition lives in another source file passed to cccc in the same
  invocation (the conventional `.h`/`.c` split), not only one whose body is
  textually present in the translation unit. The definition's body — and any
  functions it calls, transitively — is pulled into the comptime program on
  demand when a call to it is generated. A function that is only declared
  (defined in a separately-linked object, or missing) still cannot be called
  from comptime code, now with a diagnostic that names the two supported ways
  to make the body reachable.
- Fixed: a comptime-generated function whose signature named a tagless
  `typedef struct { ... } T;` produced an incomplete type under
  `-c=native`/`-c=generated`.
- Fixed: a struct-returning call left a dead, unused local in every
  generated wrapper around it.
- Added: `WithLoop(loop) { ... }` / `LoopSetBody(loop, body)`, a scoped
  reflection API for attaching an eagerly-parsed `Quote()` body to a
  builder-constructed loop (`MakeWhile`/`MakeFor`/`MakeDoWhile`) so its
  `break`/`continue` resolve against that loop.
- Fixed: `MakeWhile`/`MakeFor`/`MakeDoWhile` never assigned their loop
  node's break/continue targets, so a `break`/`continue` in an attached
  body had nothing to bind to.
- Fixed: a `Quote()` template could silently resolve and write a comptime
  program's own spliced copy of a same-named runtime global, instead of
  the runtime global itself, with no diagnostic; this is now a compile
  error.
- Fixed: a `[[cccc::comptime]]` function returning a struct/union/vector by
  value crashed the comptime interpreter with "return buffer pool was never
  allocated"; a fault during comptime execution now aborts the build with a
  diagnostic instead of silently continuing.
- Fixed: a comptime macro or `QuoteLazy()` fragment that produced a computed
  `goto *p`, a bare `case` label, or an `asm(...)` statement was not
  recognised as a statement, so in statement position it failed with an
  opaque "unsupported expression node kind" instead of splicing in, and in
  expression position it skipped the clear "returned a statement" diagnostic.
  `Quote("asm(\"...\");")` also now parses cleanly regardless of the trailing
  semicolon.
- Fixed: a `#include @comptime`/`#include @shared` directive immediately
  followed by a `[[cccc::comptime]]` definition written on one line
  segfaulted the compiler; a related shape corrupted the comptime macro
  isolation stack instead.
- Added: `QuoteLazy(tmpl, ...)`/`QuoteLazyN(tmpl, nodes, count)`, a deferred
  form of `Quote`/`QuoteN` that captures a template without parsing it, so a
  loop-body fragment built by its own call can use `break`/`continue` or
  reference a variable that is only in scope once it is spliced into a
  separately-built outer `Quote()`/`QuoteLazy()` template.
- Fixed: a label reference inside a `Quote()`/`QuoteLazy()` template was never
  resolved — comptime expansion is a post-parse pass, so a `goto label` in a
  template emitted no jump (a silent fall-through) and `&&label` yielded a
  null code address that crashed a later `goto *p` with a spurious stack
  overflow. Template labels are now hygienic (private to the template), a
  `goto` may target a label in the enclosing function, and any still-
  unresolved label reference is a hard internal error rather than
  silently-wrong code.
- Changed: a `goto` in the enclosing function can no longer target a label
  defined only inside a `Quote()`/`QuoteLazy()` template spliced into it —
  it is `use of undeclared label`, matching what an eager `Quote()` already
  reported and making the eager and deferred paths consistent.
- Added: `--compiler-family=gcc|clang|auto` selects which host compiler
  family CCCC's front end models where gcc and clang genuinely disagree on
  type compatibility — a top-level `_Atomic`, an array element's `_Atomic`,
  and a function type's own return-type `const`/`volatile` in
  `__builtin_types_compatible_p`. Default `gcc` (unchanged); `clang` matches
  a stock clang install; `auto` probes `CCCC_NATIVE_CC`. `__CCCC_COMPILER_FAMILY__`
  is predefined for source-level branching. See `man/TYPES.md`.
- Fixed: `__builtin_types_compatible_p` reported two arrays of equal length
  (and a complete array vs an unspecified-length one of the same element
  type) as incompatible; both are compatible.
- Fixed: `__builtin_types_compatible_p` ignored `_Atomic` below the top
  level, so `_Atomic int *` / `int *` and `void(_Atomic int)` / `void(int)`
  compared equal. `_Atomic` is now significant at a pointee, a function
  parameter, and a function return position.
- Fixed: a `_Generic` selection with an `_Atomic`-qualified association
  alongside its unqualified counterpart (`_Atomic int:` and `int:`) was
  rejected as specifying two compatible types; it is now accepted, and an
  unqualified controlling expression selects the unqualified arm — its own
  top-level `_Atomic` is dropped by lvalue conversion, matching `const`.
- Added: `_BitInt(N)` with `N > 128` lowers to a real multi-word runtime
  under `-c=native`, `-m`, and `-c=generated` — an emitted little-endian
  word-array container plus width-generic arithmetic, bitwise, shift,
  comparison, and cast helpers shared verbatim with the VM. A `_BitInt(N)`
  bit-field member with `N > 128` is still rejected (no legal C spelling for
  the container as a bit-field type).
- Changed: under `-c=native`, an explicit `--std=` naming a standard the host
  compiler accepts no `-std=` spelling of is now a hard error naming the
  compiler and the spellings tried, raised before compilation — previously the
  rejected spelling was forwarded anyway, producing a confusing host-compiler
  failure. Without an explicit `--std=` the behaviour is unchanged (no `-std=`
  is forwarded and the host uses its own default). The host `-std=` probe also
  now prefers the `gnu<NN>` spelling of a standard over the strict ISO `c<NN>`
  one consistently. See `man/NATIVE.md`.
- Fixed: under `-c=native`, an explicit `--std=` naming a pre-C11 standard
  (`c99`, `c89`) used to forward a `-std=` spelling of that same older
  standard, even though the serializer emits a fixed GNU C11 floor
  (`_Atomic`, `_Thread_local`, `_Alignas`/`_Alignof`, `_Static_assert`,
  `_Complex`) unconditionally, regardless of `--std=` — a plain `struct`
  definition alone triggers layout-guard `_Static_assert`/`_Alignof`
  output, no explicit C11 construct required in the source. The probed
  spelling is now floored at C11: `--std=c99` resolves a C11 spelling
  (`gnu11`, `gnu1x`, `c11`, or `c1x`) instead, failing only if the host
  compiler accepts none of those. When no `--std=` is passed at all and the
  host accepts no `-std=` spelling, its own default dialect is now checked
  against the same C11 floor, failing with a clear diagnostic rather than
  handing the emitted file to a host whose default dialect cannot parse it.
  See `man/NATIVE.md`.
- Fixed: `--build` native targets forwarded an explicit `--std=` to each
  target's compiler byte-for-byte, with no spelling probe at all — a build
  script run with `--std=c23` on a host compiler that only accepts
  `-std=c2x` (not `-std=c23`) failed with a host-compiler parse error
  instead of a CCCC diagnostic, the same asymmetry already handled for
  `-c=native`. `--build` targets now probe each target's own resolved
  compiler for a spelling of the *named* standard (trying the user's own
  `c`/`gnu` prefix, e.g. `c23` then `c2x`, never falling back to the other
  prefix and never descending to an older standard — a `--build` target
  compiles the user's own source, not serializer output, so neither of
  `-c=native`'s dialect-widening reasons apply here). An unhonourable
  `--std=` now fails only that target, with `--build-keep-going` and
  `--build-dry-run` both behaving as expected. See `man/BUILD_MODE.md`.
- Fixed: a `for (;;) { ...; goto DONE; } DONE: return ...;` function body
  (a named label statement whose own statement unconditionally returns)
  wrongly failed with "control reaches end of non-void aggregate function"
  for a struct/union return type, because the missing-return check
  (`statement_terminates`) never looked inside a labeled statement. It now
  recurses into a label's own statement the same way it already does for a
  block's last statement or an `if`/`else` pair.
- Fixed: several standard-library coverage gaps surfaced by attempting a
  self-hosting compile of cccc's own source under `-c=native`: `<string.h>`
  was missing `strtok`; `<glob.h>` was missing `GLOB_TILDE` and several other
  GNU/BSD extension bits; `<sys/stat.h>`'s `struct stat` had no
  `st_atime`/`st_mtime`/`st_ctime` compatibility macros aliasing the
  timespec-based fields it does declare; `<stdio.h>` was missing
  `asprintf`/`vasprintf` entirely (now also registered for VM/bytecode-mode
  execution, including a from-scratch `vasprintf` implementation for hosts
  routed through the custom `%b`/`%B` printf engine).
- Fixed: `<locale.h>` had no `#ifdef __CCCC__` / `#include_next <locale.h>`
  hand-off (unlike `<pthread.h>`/`<fenv.h>`/`<stdio.h>`, #1021/#1022/#1040),
  so a real host compiler reprocessing it under `-c=native`/`-c=generated`
  saw its own trimmed `struct lconv` collide with the real host's — surfaced
  by the same self-hosting attempt. `<ctype.h>`, `<langinfo.h>`, and
  `<monetary.h>` also quote-included `"locale.h"` rather than `<locale.h>`,
  which would have bypassed the hand-off entirely (#1070's own rule against
  a bundled header quote-including one with a hand-off) — switched to
  angle brackets.