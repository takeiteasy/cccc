# Changelog

All notable changes to CCCC are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Version history
before the 0.1.0 reset is not relisted here — see the ticket tracker and
`git log` for the historical record.

## [0.1.0] - Unreleased

- Initial release.
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