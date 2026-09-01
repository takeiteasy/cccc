# Changelog

All notable changes to CCCC are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Version history
before the 0.1.0 reset is not relisted here — see the ticket tracker and
`git log` for the historical record.

## [0.1.0] - Unreleased

- Initial release.
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
- Fixed: a `#include @comptime`/`#include @shared` directive immediately
  followed by a `[[cccc::comptime]]` definition written on one line
  segfaulted the compiler; a related shape corrupted the comptime macro
  isolation stack instead.
- Added: `QuoteLazy(tmpl, ...)`/`QuoteLazyN(tmpl, nodes, count)`, a deferred
  form of `Quote`/`QuoteN` that captures a template without parsing it, so a
  loop-body fragment built by its own call can use `break`/`continue` or
  reference a variable that is only in scope once it is spliced into a
  separately-built outer `Quote()`/`QuoteLazy()` template.