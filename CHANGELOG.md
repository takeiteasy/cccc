# Changelog

All notable changes to CCCC are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Version history
before the 0.1.0 reset is not relisted here — see the ticket tracker and
`git log` for the historical record.

## [Unreleased]

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
