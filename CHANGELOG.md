# Changelog

All notable changes to CCCC are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Version history
before the 0.1.0 reset is not relisted here — see the ticket tracker and
`git log` for the historical record.

## [0.1.0] - Unreleased

- Initial release.
- A VLA local, or a pointer-to-VLA local initialized from one, can now be
  read across a nested (GNU) function's static link under `-c=native`/`-m`
  — previously rejected outright. A fully multi-dimensional VLA (every
  extent runtime-sized) is still rejected, with a narrower diagnostic.
- VLA brace initialization (`int v[n] = {...}`, a CCCC-only extension no
  reference compiler accepts) is retained rather than removed — its
  correctness defects were already resolved by an earlier fix, and excess
  initializers now have a test pinning the runtime bounds trap that catches
  them at `-2`/`-3`.
- A new `-Wexcess-init` warning (part of `-Wall`) diagnoses a brace
  initializer that supplies more elements than the target holds — a
  fixed-size array, a struct, a GNU vector, or a too-long string
  initializer — matching GCC/clang. VLA brace initialization is
  deliberately excluded: its length isn't known until runtime, so excess
  elements there stay caught by the runtime bounds trap above, not this
  warning.
