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
- A small all-non-negative enum (every enumerator fits `int`, no fixed `:
  type` underlying type) now gets underlying type `unsigned int`, matching
  gcc/clang exactly — previously stayed plain signed `int`. Each enumerator
  *identifier* still has type `int` per C17/C23 6.7.2.2p3, unless the enum
  has a fixed underlying type of its own.
- A global union initializer where no member spans the union's full
  (alignment-padded) size — e.g. `union U { char c[3]; short s; }` — now
  serializes under `-c=native`/`-m` instead of being refused; the untouched
  tail past the largest member is always zero, relocation-free alignment
  padding. A union whose largest member is itself an anonymous struct/union
  no longer crashes the serializer either — it's flattened into transparent
  designators in the enclosing initializer.
- `_Generic` and `__builtin_types_compatible_p` now match a controlling
  expression of enumerated type against an association naming its
  underlying integer type (`_Generic((enum G){0}, unsigned int: …)` for an
  all-non-negative `enum G`), and the reverse direction too, matching
  gcc/clang. Two separately declared enums remain mutually incompatible. A
  `_Bool` association arm and a tagged `struct`/`union` association arm now
  match instead of falling through to `default`. Spelling a C23
  `enum E : T` underlying type inside an association
  (`_Generic(x, enum E : int : …)`) is a compile error — the `:` there is
  the association colon.
- `_Generic` now rejects a selection with more than one `default`
  association or with two associations that specify compatible types
  (C23 6.7.11p2), matching GCC/clang — previously the first matching arm
  was taken silently. `long` and `long long` associations in the same
  `_Generic` are still accepted (CCCC models them as one type), as is the
  `char *` / `const char *` const-correct dispatch idiom.
