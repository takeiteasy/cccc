# Warnings and Diagnostics

gcc/clang-style warning categories. Everything is off by default except `-Wunknown-attributes`, `-Wopenmp` and `-Wchecked-regions`.

## Controls

| Flag | Effect |
|---|---|
| `-W<name>` | Enable one category |
| `-Wno-<name>` | Disable one category |
| `-Wall`, `-Wextra` | Enable the groups in [Names](#names) |
| `-Werror`, `--Werror` | Promote every enabled warning to an error |
| `-Werror=<name>` | Enable one category and make it an error |
| `-Wno-error=<name>` | Keep one category a warning, even after `-Werror` |
| `--json` | One JSON object per diagnostic on stderr ([below](#machine-readable-output)) |

Options apply left to right. `-Werror=<name>` is sticky: a later `-Wno-error=<name>` has no effect, so use `-Wno-<name>` to remove the category and its promotion.

## Pragmas

```c
#pragma cccc diagnostic push                 // save the state
#pragma cccc diagnostic pop                  // restore it
#pragma cccc diagnostic ignored "-Wunused"   // suppress a category
#pragma cccc diagnostic warning "-Wunused"   // enable as a warning
#pragma cccc diagnostic error   "-Wunused"   // promote to an error
```

`push` and `pop` nest; an unmatched `pop` reports `-Wcpp`. `#pragma GCC diagnostic` and `#pragma clang diagnostic` act the same, so existing headers work unchanged, and unlike the `cccc` form they are passed through to `-c=generated` output. State is per token and takes effect at the pragma's position, including for the implicit-return warning at the end of a function.

## Per-test flags

`flags=` on a test applies warning options to that test only ([testing-flags.md](testing-flags.md)):

```c
[[cccc::test(return = 42, flags = "-Werror=conversion")]]
int test_no_conversion(void) { return 42; }
```

## Machine-readable output

`--json` replaces the human format; the `N warnings generated.` line is suppressed.

```json
{"severity":"warning","file":"foo.c","line":10,"column":5,"message":"unused variable 'x'","option":"-Wunused"}
{"severity":"error","file":"foo.c","line":20,"column":1,"message":"expected ';'","option":null}
```

| Field | Type | Meaning |
|---|---|---|
| `severity` | string | `"warning"` or `"error"` |
| `file` | string | Source path |
| `line`, `column` | number | 1-based |
| `message` | string | Diagnostic text |
| `option` | string or null | The `-W` name, or `null` for hard errors |

## Names

Group: **A** is in `-Wall`, **E** in `-Wextra`, **D** on by default, blank is standalone. `-Wconversion` also enables `sign-conversion` and `float-conversion`.

| Name | Group | Warns when |
|---|---|---|
| `unused` | A | Unused variables and similar |
| `implicit-function-declaration` | A | A call has no visible declaration[^implicit] |
| `implicit-int` | A | Implicit `int` |
| `return-type` | A | Missing or mismatched return |
| `shadow` | A E | A declaration shadows another |
| `format` | A | Format string problems (see also `--format-string-checks`) |
| `conversion` | A E | Narrowing integer conversion; umbrella for the next two |
| `sign-conversion`, `float-conversion` | | Implied by `conversion` |
| `sign-compare` | A E | Signed/unsigned comparison |
| `pointer-arith` | A E | Suspicious pointer arithmetic |
| `pedantic` | A | Non-ISO constructs |
| `deprecated` | A | Use of a deprecated entity |
| `cpp` | A | `#warning`, unmatched diagnostic `pop`, unknown `__has_*` operator |
| `extra-tokens` | A | Extra tokens after a directive |
| `large-file-embed` | A | A large `#embed` |
| `cccc-macro` | A | CCCC comptime macro issues |
| `comptime-block-leak` | E | A `#pragma cccc comptime begin` block in an included header is left unclosed at EOF and auto-closed |
| `ignored-features` | A | A construct the VM does not honour: `_Atomic`, ignored attributes, file-scope `asm`, extended `asm` with outputs or `goto` labels (not raised under `-c=native`/`-m`/`-c=generated`, where the asm runs) |
| `attributes` | A | Unknown attribute names (names `__has_attribute` knows are silent), and misuse such as `sentinel` on a non-variadic function |
| `unknown-attributes` | D | An `@name` with no registered handler (`@json_skp`). `[[name]]` and `__attribute__((name))` report under `attributes`. Silence with `-Wno-unknown-attributes` |
| `openmp` | D | An `ordered depend(sink: ...)` that waits on the current or a later iteration, or on one the loop never runs; the sink is ignored ([openmp.md](openmp.md#doacross-loops)). Silence with `-Wno-openmp` |
| `checked-regions` | D | A [checked-region](safety-checked-regions.md#downgrading-to-warnings) violation. An error by default; `-Wno-error=checked-regions` makes it a warning. `-Wno-checked-regions` does not silence it |
| `checked-calls` | | A call from a [checked region](safety-checked-regions.md#call-boundaries) to a prototype with an unchecked pointer, or a checked pointer passed to a raw parameter. Header declarations and `[[cccc::trusted]]` prototypes are exempt |
| `nodiscard` | A | Discarding a `nodiscard` result |
| `fallthrough` | E | Implicit switch fallthrough |
| `strict-prototypes` | E | A function declared without a prototype |
| `old-style-definition` | E | K&R function definition |
| `redundant-decls` | E | Same name declared twice with the same linkage in one scope |
| `static-array-size` | A | An array argument has fewer elements than the parameter's `static N` |
| `discarded-qualifiers` | A | A pointer assignment drops a qualifier |
| `duplicated-branches` | | `then` and `else` are identical |
| `duplicated-cond` | | A condition repeats in an `if`/`else if` chain |
| `unused-value` | | A discarded expression has no side effects (`x + y;`) |
| `multichar` | A | `'ab'`-style constants |
| `main` | A | Suspicious `main()` signature |
| `switch-default` | A | `switch` has no `default:` |
| `switch-bool` | A | `switch` on a `bool` |
| `switch` | A | Enum `switch` missing an enumerator (unless `default:` exists) or with a `case` matching none |
| `switch-enum` | | Like `switch`, even when `default:` exists |
| `enum-compare` | A | Comparing values of different enum types |
| `float-equal` | A | `==` or `!=` on floating-point operands |
| `shift-negative-value` | A | Negative constant shift amount |
| `shift-overflow` | A | Shift amount at or past the promoted type's width |
| `logical-op` | A | Constant operand of `&&` or `||` |
| `tautological-compare` | A | Self-comparison or an always-true/false unsigned range check (neither fires inside `#if`/`#elif`) |
| `sizeof-pointer-memaccess` | A | `sizeof(pointer)` as the size in `memset`/`memcpy`/`memmove`/`memcmp` |
| `incompatible-pointer-types` | A | Implicit pointer conversion between incompatible pointee types (not `void *`) |
| `int-conversion` | A | Integer to pointer or back without a cast, in assignment, initialization, `return` and prototyped arguments; the null constant `0` is exempt; file-scope initializers are not checked |
| `cast-qual` | | A cast removes `const`, `volatile` or `restrict` |
| `cast-align` | | A cast raises the pointer target's alignment |
| `missing-prototypes` | | A non-static function is defined without a prior prototype |
| `missing-declarations` | | A non-static, non-inline function is defined without any prior declaration |
| `override-init` | A | A later designator overrides an earlier one (`{.x=1, .x=2}`); also a different union member, and fields through anonymous or named nested members |
| `excess-init` | A | A brace initializer has more elements than the target holds (array, struct, vector, or an over-long string). Warns once per list. Flexible arrays and VLAs are never flagged |
| `auto-declarator` | A | `auto *p = &x;` or several `auto` declarators; silent under `--compiler-family=clang` ([types.md](types.md#inferred-declarations)) |
| `unused-macros` | | A non-system `#define` is never expanded |
| `nonnull` | A | A null argument to a `nonnull` parameter, or null returned from `returns_nonnull` ([attributes-nonnull.md](attributes-nonnull.md)) |
| `maybe-nonnull` | | The same, but only on some paths or via a function that may return null; higher false-positive rate |
| `sentinel` | A | Missing or non-pointer-typed `NULL` sentinel ([attributes-gnu.md](attributes-gnu.md#sentinel)) |
| `designated-init` | | Positional initializer for a `designated_init` struct ([attributes-gnu.md](attributes-gnu.md#designated_init)) |
| `native-name-collision` | A | `-m`/`-c=native`/`-c=generated`: a header-exposed enumerator collides with a file-scope identifier in a unit that does not include that header. Points at the declaration and the header, since the host compiler's own message names a deleted temp file |

Registered but emitting no compile-time diagnostic (runtime `-2`/`-3` safety covers them): `null-dereference`, `restrict`, `array-bounds`, `stringop-overflow`, `stringop-truncation`.

[^implicit]: A hard, unsuppressible error at `--std=c99` and later (and the `gnu` forms), as ISO C99 6.5.2.2p1 requires; `-Wno-implicit-function-declaration` only silences it at `--std=c89`/`gnu89`, where the call resolves as a variadic `int f(...)`. Under `-c=native` it is always a hard error, because the guessed signature is never emitted into the generated C.
