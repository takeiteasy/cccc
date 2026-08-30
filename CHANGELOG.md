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
- Fixed: a `[[cccc::comptime]]` function returning a struct/union/vector by
  value crashed the comptime interpreter with "return buffer pool was never
  allocated"; a fault during comptime execution now aborts the build with a
  diagnostic instead of silently continuing.
- Fixed: a `#include @comptime`/`#include @shared` directive immediately
  followed by a `[[cccc::comptime]]` definition written on one line
  segfaulted the compiler; a related shape corrupted the comptime macro
  isolation stack instead.