# Warnings and Diagnostics

CCCC supports gcc/clang-style warning category controls for suppressible
compiler diagnostics. Warnings are disabled by default; enable categories with
`-W<name>`, `-Wall`, or `-Wextra`.

## Controls

- `-W<name>` enables one warning category.
- `-Wno-<name>` disables one warning category.
- `-Wall` enables the common warning set.
- `-Wextra` enables extra checks beyond `-Wall`.
- `-Werror` or `--Werror` promotes all enabled warnings to errors.
- `-Werror=<name>` enables one category and promotes it to an error.
- `-Wno-error=<name>` keeps one enabled category as a warning, even after
  `-Werror`.

Options are processed left to right, so later flags override earlier flags.
Exception: `-Werror=<name>` is **sticky**. Once a category is promoted to an
error with `-Werror=<name>`, a subsequent `-Wno-error=<name>` has no effect.
Use `-Wno-<name>` to fully disable the category, including its error promotion.

## Per-Test Warning Flags (in `[[cccc::test]]` suites)

Warning flags can be applied per-test inside `[[cccc::test]]` suite files using
the `flags=` attribute. This triggers a lazy recompile of the whole program with
the specified warning configuration before running that test:

```c
// Compile this test with -Wpedantic active
[[cccc::test(return = 42, flags = "-Wpedantic")]]
int test_pedantic_clean(void) { return 42; }

// Promote conversion warnings to errors for this test
[[cccc::test(return = 42, flags = "-Werror=conversion")]]
int test_no_conversion(void) { return 42; }
```

All CLI warning flags are accepted: `-W<name>`, `-Wno-<name>`, `-Wall`,
`-Wextra`, `-Wpedantic`, `-Werror`, `-Werror=<name>`, `-Wno-error=<name>`.
See [TEST_MODE.md](TEST_MODE.md) for the full per-test flags reference.

## Pragma-Based Suppression

Within source files, `#pragma cccc diagnostic` controls warning state inline:

```c
#pragma cccc diagnostic push           // save current warning state
#pragma cccc diagnostic pop            // restore saved state
#pragma cccc diagnostic ignored "-Wunused"   // suppress a category
#pragma cccc diagnostic warning "-Wunused"   // enable a category as warning
#pragma cccc diagnostic error   "-Wunused"   // promote a category to error
```

`push` and `pop` nest: each `push` saves the current state onto a stack, and
the matching `pop` restores it. Unmatched `pop` emits a `-Wcpp` diagnostic.

`#pragma GCC diagnostic` and `#pragma clang diagnostic` are also accepted and
act identically — this allows headers that already use GCC-style pragmas to
suppress warnings without modification. Unlike `#pragma cccc diagnostic`, the
GCC/clang forms are passed through to `-c=generated` output so downstream compilers also
see them.

Pragma state is per-token and takes effect immediately at the pragma's source
position, so it correctly suppresses warnings on variables declared or used
after the pragma, including the implicit-return warning at the end of a
function.

## Machine-Readable Output

Pass `--json` to emit one JSON object per diagnostic to
stderr instead of the human-readable format:

```json
{"severity":"warning","file":"foo.c","line":10,"column":5,"message":"unused variable 'x'","option":"-Wunused"}
{"severity":"error","file":"foo.c","line":20,"column":1,"message":"expected ';'","option":null}
```

Fields:

| Field | Type | Description |
|-------|------|-------------|
| `severity` | string | `"warning"` or `"error"` |
| `file` | string | Source file path |
| `line` | number | 1-based line number |
| `column` | number | 1-based column number |
| `message` | string | Diagnostic text |
| `option` | string or null | `-W` flag name, or `null` for hard errors |

The trailing summary line (`N warnings generated.`) is suppressed in JSON mode.

## Supported Warning Names

- `unused`
- `implicit-function-declaration` (warning-only at `--std=c89`/`gnu89`; a hard,
  unsuppressible error at C99 and later — CCCC's own default — and always
  under `-c=native` regardless of `--std=`; see below)
- `implicit-int`
- `return-type`
- `shadow`
- `format` (see also `-F` / `--format-string-checks`)
- `conversion` (umbrella; enables `sign-conversion` and `float-conversion` too)
- `sign-conversion`
- `float-conversion`
- `sign-compare`
- `pointer-arith`
- `pedantic`
- `deprecated`
- `cpp`
- `extra-tokens`
- `large-file-embed`
- `cccc-macro`
- `comptime-block-leak` — warns when a `#pragma cccc comptime begin` block in an included header is left unclosed at EOF and is auto-closed (part of `-Wextra`)
- `ignored-features`
- `attributes` — general attribute-usage diagnostics, e.g. `sentinel`/`[[gnu::sentinel]]` applied to a non-variadic function ("sentinel attribute only applies to variadic functions")
- `nodiscard`
- `fallthrough`
- `strict-prototypes`
- `discarded-qualifiers`
- `null-dereference` — registered; no compile-time diagnostic emitted (covered by `-S2`/`-S3` runtime safety)
- `restrict` — registered; no compile-time diagnostic emitted (covered by runtime safety)
- `array-bounds` — registered; no compile-time diagnostic emitted (covered by `-S2`/`-S3` runtime safety)
- `stringop-overflow` — registered; no compile-time diagnostic emitted (covered by runtime safety)
- `stringop-truncation` — registered; no compile-time diagnostic emitted (covered by runtime safety)
- `duplicated-branches` — warns when the `then` and `else` bodies of an `if` statement are structurally identical
- `duplicated-cond` — warns when a condition is repeated in an `if`/`else if` chain
- `unused-value` — warns when an expression whose result is discarded has no side effects (e.g. `x + y;` as a statement)
- `multichar` — warns on multi-character character constants such as `'ab'` or `'abc'`
- `main` — warns on suspicious `main()` signatures: non-`int` return type, wrong parameter count (not 0 or 2), wrong first parameter type, or wrong second parameter type
- `switch-default` — warns when a `switch` statement has no `default:` label
- `switch-bool` — warns when the controlling expression of a `switch` has boolean type (`_Bool` / `bool`)
- `switch` — for a `switch` on an enum-typed condition: warns when an enumerator has no matching `case` (unless a `default:` is present), and warns when a `case` label's value doesn't match any enumerator of that enum
- `switch-enum` — like `switch`'s missing-enumerator check, but fires even when a `default:` is present
- `float-equal` — warns on direct `==` or `!=` comparisons between floating-point operands
- `shift-negative-value` — warns when the shift amount is a negative integer constant
- `shift-overflow` — warns when the shift amount equals or exceeds the promoted type's bit-width
- `logical-op` — warns when a constant expression appears as an operand of `&&` or `||`
- `tautological-compare` — warns on self-comparisons and unsigned range checks that are always true or false
- `sizeof-pointer-memaccess` — warns when `sizeof(pointer)` is passed as the size argument to `memset`, `memcpy`, `memmove`, or `memcmp`
- `incompatible-pointer-types` — warns on implicit pointer assignments or argument passing where the pointee types are incompatible (excluding `void *`); part of `-Wall`
- `cast-qual` — warns when an explicit cast removes `const`, `volatile`, or `restrict` from the pointed-to type
- `cast-align` — warns when an explicit cast increases the alignment requirement of the pointer target type
- `missing-prototypes` — warns when a non-static function is defined without a prior full prototype declaration
- `missing-declarations` — warns when a non-static, non-inline function is defined without any prior declaration
- `redundant-decls` — warns when the same name is declared more than once with the same linkage in the same scope; part of `-Wextra`
- `override-init` — warns when a later designator in a compound initializer overrides an earlier one (e.g. `{.x=1, .x=2}`), naming the overridden field; part of `-Wall`. For a union, this also fires when a *different* member's designator overrides one already set (e.g. `{.i=1, .f=2}`, since union members alias — matching gcc/clang), and covers a field reached through an anonymous or named struct/union member (e.g. `{.i=1, .i=2}` where `i` is a field of an anonymous union member, or `{.u.i=1, .u.i=2}` for a named nested union)
- `unused-macros` — warns when a `#define` that is not in a system header is never expanded anywhere in the translation unit; standalone only (not part of `-Wall` or `-Wextra`)
- `nonnull` — warns when a null argument is passed to a parameter marked `nonnull`/`nonnull(N,...)`, or when null is returned from a function marked `returns_nonnull`. Catches both a literal/constant-folded null and, via a flow-sensitive pass, a value provably null on *every* live path that reaches the call through a local variable; part of `-Wall`. Also catches a call to a function whose whole-TU summary (see `maybe-nonnull` below) proves it returns null on *every* path — but only when `-Wmaybe-nonnull` is also passed, since that flag gates the interprocedural pass that discovers the fact in the first place
- `maybe-nonnull` — companion to `nonnull`: warns on a value that is null on only *some* live paths reaching a nonnull-marked parameter or `returns_nonnull` return (e.g. `int *p = 0; if (cond) p = &x; f(p);`), via real dataflow over `if`/ternary/`&&`/`||`, loops (a bounded back-edge fixpoint, with `break`/`continue` envs tracked and joined in at the right point), and `switch` (a per-case join against the switch's entry state, with `break` envs joined into the exit). A construct the fixpoint/join scheme can't safely model — a `goto`/label anywhere inside, a computed goto, or Duff's device (a `case` label reachable from a loop body without an intervening `switch` of its own) — falls back to the original conservative scheme (every local assigned anywhere inside is reset to unknown), so it never produces a false positive, only a possibly-missed warning. Also covers a limited interprocedural case: a whole-translation-unit summary, iterated to a fixpoint, flags a pointer-returning function with a provable null-returning path — either a literal `return 0;`/`return NULL;`, or a `return` of a call to another already-flagged function (a transitive chain of any depth, converging regardless of source order). A direct call to a flagged function is treated as maybe-null at its call sites — whether assigned to a local first or used inline as the argument/return expression itself (e.g. `handle(maybe_null())`, `return maybe_null();`) — unless the summary proved the callee null on *every* path, in which case it's reported under `nonnull` instead (see above). An unannotated external/declaration-only callee is never assumed to maybe-return null. Higher false-positive rate on real code than plain `nonnull`, so standalone only — not part of `-Wall` or `-Wextra`
- `sentinel` — warns on a call to a function marked `sentinel`/`sentinel(N)` whose expected trailing variadic argument is not a literal, pointer-typed `NULL` (`NULL`/`(void*)0`/`nullptr`); a literal but non-pointer-typed `0` gets a distinct "bare 0 is not a pointer" message, and a fully-missing terminator gets "missing sentinel in function call". Also warns if the call doesn't supply enough variadic arguments for the sentinel position to exist. Purely syntactic (no flow analysis — a variable holding `NULL` still warns); part of `-Wall`. Applying `sentinel` to a non-variadic function is a separate, declaration-time warning under `attributes` (see above), not this flag
- `designated-init` — warns on a positional member initializer (`{1, 2}`, or the positional tail of a mixed literal like `{.a=1, 2}`) targeting a struct type marked `__attribute__((designated_init))`/`[[gnu::designated_init]]`. Purely syntactic, parse-time-only; a brace-less copy-initializer (`struct S a = b;`) and C23 empty-init `{}` are never flagged. Unlike GCC, **standalone only — not part of `-Wall` or `-Wextra`**, since CCCC enables no warnings by default
- `int-conversion` — warns on an implicit conversion between an integer and a pointer with no cast (e.g. `const char *p = 'a';` or `int n = some_ptr;`), covering assignment/scalar initialization, `return`, and prototyped call arguments. Suppressed when the source is the null pointer constant `0`, matching the standard exemption for `T *p = 0;`. Not checked for file-scope/global initializers (those take a separate constant-evaluation path). Part of `-Wall`
- `native-name-collision` — `-m`/`-c=native`/`-c=generated` only: warns when the serializer's rename passes find a colliding name it cannot rename apart, so the generated C is left with a genuine collision for the host compiler to report. Currently covers one case: a header-exposed `enum`'s enumerator colliding with a plain file-scope identifier (a `static`, an `extern` global, or a function) declared in a translation unit that does not include that header — neither the enumerator (the replayed `#include` binds it textually) nor the Obj (renaming it would change an emitted symbol, or widen an existing "only rename dups" rule) can safely be renamed. Points at the colliding declaration and names the enumerator and the header that exposes it, so the user isn't left with only the host compiler's own diagnostic — which under `-c=native` names a temporary file that is deleted before the invocation returns. Part of `-Wall`
- `excess-init` — warns when a brace initializer supplies more elements than the target holds: a fixed-size array (`int a[1] = {1, 2, 3};`), a struct (`struct S s = {1, 2, 3};`, including the positional tail of a mixed literal past the last designator-reached member, e.g. `{.b = 1, 2}`), a GNU `vector_size` vector, or a string initializer too long for its destination array (`char c[3] = "abcd";`) — the last case names how many characters were supplied against how many were available, matching GCC's wording; an exact-fit string with the trailing NUL dropped (`char a[4] = "abcd";`) is legal C and never flagged. Warns once per initializer list, not once per surplus element (unlike GCC), so a nested excess (`int a[2][1] = {{1, 2}, {3}};`) warns once on the inner list that actually overflows. A flexible-size array/string (`int x[] = {...}`/`char c[] = "..."`) and a flexible array member are never flagged — their length comes from the initializer itself, so there is no fixed bound to exceed. Deliberately excludes VLA brace initialization (`int v[n] = {...}`): a VLA's bound isn't known until runtime, so excess elements there aren't statically checkable even in principle — the resulting out-of-bounds store is instead caught by the ordinary runtime bounds machinery at `-2`/`-3`. Part of `-Wall`

`implicit-function-declaration` is a hard error, not merely a warning, at
`--std=c99`/`c11`/`c17`/`c23` (and their `gnu*` variants) — matching ISO C99
6.5.2.2p1's constraint that a called function have a visible declaration, and
what every real host C compiler does at those standards. `-Wno-implicit-
function-declaration` has no effect there; it only silences the warning at
`--std=c89`/`gnu89`, where the call still resolves (as a variadic
`int f(...)`) exactly as it always has. Under `-c=native` it is always a hard
error, even at `--std=c89`: the guessed implicit signature is deliberately
never emitted into the generated C (it could collide with the real one from a
replayed header), so a real host compiler would reject the reference anyway
— CCCC reports it as its own error up front instead.

`conversion` is an umbrella name: `-Wconversion` enables `sign-conversion` and
`float-conversion` as well as the integer-narrowing check.

`all` and `extra` are group names used by `-Wall`, `-Wno-all`, `-Wextra`, and
`-Wno-extra`.
