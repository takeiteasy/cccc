# Changelog

All notable changes to CCCC are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Version history
before the 0.1.0 reset is not relisted here — see the ticket tracker and
`git log` for the historical record.

## [0.1.0] - Unreleased

- Initial release.
- `examples/ccccl/`, the comptime-Lisp worked example, has been rebuilt from
  scratch against the current comptime API: a tree IR (replacing the old
  flat postfix op-list), lexical scoping with real C parameters and
  closures captured by index (replacing dynamic, environment-passing
  scoping), fixnums/`if`/`let`/`progn`/quoted lists/`print` added to the
  language, statement-form emission (real `if`/`else`, not one nested
  ternary `return`), and self-tail-call elimination via loop-and-reassign.
  `examples/ccccl/build.c` is now a plain `Makefile` (cccc can't build
  itself as a `build.c` target yet). See `examples/ccccl/README.md`.
- `-c=native`/`-m`/`-c=generated` no longer misidentify a struct with a
  pointer or function-pointer member (a self-referential cons-cell shape,
  or a closure/callback slot) as two colliding tags when it's completed
  independently in more than one translation unit, even though every field
  is byte-for-byte identical. `same_type_or_origin()` had no structural
  fallback for `TY_PTR`/`TY_FUNC` — only pointer-identity via each type's
  `->origin` chain, which two separately-parsed TUs' pointer/function types
  never share — so `rename_colliding_type_tags()` renamed one occurrence to
  `<Tag>__cccc_dup<N>`, a spelling that never gets a body (the real one is
  supplied verbatim by the other TU's own struct text under the original
  name), failing the host compile with "incomplete definition of type" /
  "conflicting types" and no cccc-side diagnostic at all.
- A multi-translation-unit `-c=native`/`-m`/`-c=generated` build now
  correctly emits a native-accessor shim (`__cccc_stdout`/`__cccc_stderr`/
  etc., behind `stdio.h`'s `#define stdout __cccc_stdout()` and similar)
  whenever any TU actually calls it — previously `cc_link_progs()`'s
  cross-TU merge only propagated a *non-function* alias's `is_used` flag
  onto the canonical `Obj`, so a used function alias from a non-canonical
  TU was silently dropped and the shim's definition never emitted, failing
  the host compile with "use of undeclared identifier" and no cccc-side
  diagnostic.
- A compile error or warning against code produced by a comptime macro,
  an `@attr` handler, or file-scope generation now carries a "note: in
  expansion of ..." backtrace back to the source that produced it —
  previously the diagnostic pointed only at the generated location, with
  no record of how the compiler got there. Under `-j` (JSON diagnostics)
  the same chain is a `"notes"` array on the diagnostic object. See
  `man/MACROS.md` § Expansion backtrace.
- `nl_langinfo`/`nl_langinfo_l` on macOS now correctly return `""` for an
  unrecognized `nl_item` (a hole in the 0-56 canonical sequence, or
  anything out of range) instead of forwarding the bogus value straight
  to the host — previously this validation only ran on Linux. The same
  fix applies to the `-c=native` shim.
- `printf`/`snprintf`/`vprintf` and friends now honour the `L` length
  modifier: `printf("%Lf", x)` formats the `long double` instead of
  emitting the literal text `"Lf"` and silently swallowing the argument.
  `%Le`, `%Lg`, `%La` and the uppercase forms, and the `scanf` `%Lf`
  input path, are fixed the same way. A variadic `long double` argument is
  now marshalled to a host `long double` at the FFI boundary; the value is
  still computed at `double` precision. As a consequence, passing a
  `long double` to a plain `%f`/`%e`/`%g` (no `L`) is now a genuine
  argument-width mismatch — the `-F` format checker flags it, matching
  gcc/clang `-Wformat`.
- The `printf` `%n` conversion now honours its length modifier: `%hhn` /
  `%hn` / `%ln` / `%lln` / `%jn` / `%zn` / `%tn` / `%Ln` write a
  `signed char` / `short` / `long` / `long long` / `intmax_t` / `size_t` /
  `ptrdiff_t` respectively, matching glibc and Apple libc — previously every
  spelling stored a plain 4-byte `int`, truncating a wider store and
  clobbering bytes past a narrower one. The `-F` format checker's `%n` arm
  is length-modifier-aware to match (`%ln` expects an 8-byte integer
  pointer, not `int *` — either signedness, since `%n` has no signed/unsigned
  split), on both the `printf` and `scanf` sides. The `scanf` `%n` runtime
  store was already correct.
- The `h` / `hh` length modifiers on an integer conversion now truncate:
  `printf("%hd", 65536)` prints `0` and `printf("%hhx", 0x1FF)` prints `ff`,
  matching glibc and Apple libc — previously the modifier was parsed and
  discarded.
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
- `_Generic` arm *selection* and `__builtin_types_compatible_p` now honor
  pointee qualifiers: a `char *` / `const char *` pair resolves by the
  controlling type rather than by listing order (so `<string.h>`'s
  const-correct `strchr`/`strrchr`/`strstr`/`strpbrk` dispatch macros yield
  the right return type independent of arm order), and
  `__builtin_types_compatible_p(char *, const char *)` is now `0` (was `1`).
  Top-level qualifiers are still ignored — the controlling expression is
  lvalue-converted, and `__builtin_types_compatible_p(int, const int)` stays
  `1`. `_Atomic` is treated as a non-qualifier, following GCC.
- `long double _Complex` is now accepted in every specifier order
  (`_Complex long double`, `long _Complex double`, …), matching C's
  order-independent type specifiers — previously the orderings that put
  `_Complex`/`_Imaginary` before `long` were rejected as "invalid type". A
  bare `_Complex long` / `_Complex int` (GNU's complex-integer extension,
  which CCCC does not implement) is still rejected.
- A `_Complex` constant expression now folds at compile time: `I` /
  `_Complex_I` / `CMPLX()` and `+`/`-`/`*`/`/`/unary-`-`/`conj` over complex
  constants are usable in a static initializer and other constant-expression
  contexts (`static double _Complex z = 3.0 + 4.0*I;`,
  `_Static_assert(cimag(I) == 1.0, …)`) — previously rejected as "not a
  compile-time constant". Such a global serializes under `-c=native`/`-m` as
  `__builtin_complex(re, im)` (a zero imaginary part still prints as a bare
  real literal). The fold mirrors the VM's runtime complex arithmetic
  bit-for-bit. Fixed in the same change: a `float`-to-`_Complex` cast under
  `-c=native`/`-m` truncated the real part to an integer.
