# Warning Options

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
See [TESTING.md](TESTING.md) for the full per-test flags reference.

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
GCC/clang forms are passed through to `-G` output so downstream compilers also
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

## Supported Names

The warning infrastructure recognizes these category names:

- `unused`
- `implicit-function-declaration`
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
- `ignored-features`
- `attributes`
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
- `redundant-decls` — warns when the same name is declared more than once with the same linkage in the same scope (e.g. two `extern` declarations of the same variable or two forward declarations of the same function); part of `-Wextra`
- `override-init` — warns when a later designator in a compound initializer overrides an earlier one (e.g. `{.x=1, .x=2}`); part of `-Wall`
- `unused-macros` — warns when a `#define` that is not in a system header is never expanded anywhere in the translation unit; standalone only (not part of `-Wall` or `-Wextra`)

`conversion` is an umbrella name: `-Wconversion` enables `sign-conversion` and
`float-conversion` as well as the integer-narrowing check.

`all` and `extra` are group names used by `-Wall`, `-Wno-all`, `-Wextra`, and
`-Wno-extra`.

## Current Diagnostics

The current implementation wires existing warning producers into the warning
system:

- Missing declaration type specifiers use `-Wimplicit-int` and default to
  `int`.
- Calls before a declaration use `-Wimplicit-function-declaration` and create
  an external `int`-returning placeholder until an explicit declaration or
  definition is parsed.
- Invalid scalar returns and reachable ends of non-void functions use
  `-Wreturn-type`. CCCC inserts a correctly typed zero return where needed.
  Returning a value from a void function preserves the expression's side
  effects.
- Unused named local variables, parameters, labels, internal-linkage
  variables, and internal-linkage functions use `-Wunused`. Compiler-generated
  and external-linkage symbols are excluded.
- Local variables and parameters that shadow an outer variable or parameter,
  including a global variable, use `-Wshadow`.
- Uses of deprecated functions, variables, typedefs, struct/union/enum tags,
  and enumerators use `-Wdeprecated`. Attribute messages are included when
  present.
- Format string mismatches in calls to functions annotated with
  `__attribute__((format(printf/scanf, …)))` use `-Wformat`. Checking is
  gated by `-F` / `--format-string-checks`, which also enables `-Wformat`.
  The checker validates argument count and type against each format specifier,
  including length modifiers: `%ld` requires `long`, `%lu`/`%llu`/`%zu`/`%ju`
  require `unsigned long`, and `%Lf` requires `long double`. Short modifiers
  (`%hd`, `%hhd`) accept their promoted integer types since variadic arguments
  undergo default argument promotion. Standard library functions in `stdio.h`
  carry the appropriate annotations.
- `#warning` uses `-Wcpp`.
- Extra preprocessor tokens use `-Wextra-tokens`.
- Large `#embed` files use `-Wlarge-file-embed`.
- `__cccc_macro_warning_at` uses `-Wcccc-macro`.
- Pre-standard use of supported language extensions uses `-Wpedantic`:
  `//` comments before C99, `long long` before C99, variable-length arrays
  before C99, compound literals before C99, designated initializers before
  C99, mixed declarations and statements before C99, anonymous structs/unions
  before C11, `_Generic` before C11, `[[...]]` attributes before C23, and
  binary integer literals before C23. These extensions compile unless
  `-Wpedantic` is promoted with `-Werror=pedantic` or `-Werror`.
- Function declarations and definitions with an empty parameter list `()` in
  pre-C23 modes use `-Wstrict-prototypes`. In C23, `()` is a full prototype
  equivalent to `(void)` and no warning is emitted. Enabled by `-Wextra`.

Implicit functions that remain unresolved are hard errors during code
generation. Missing return values for struct and union functions are also hard
errors because CCCC cannot synthesize a safe aggregate value.

C23 digit separators remain hard errors before C23 rather than pedantic
warnings.

Return fallthrough analysis recognizes returns, block sequencing, and complete
`if`/`else` branches. It conservatively treats loops, switches, and gotos as
potentially reaching the end of a function. Falling through `main` returns zero
without a warning.

`__attribute__((unused))` and `[[maybe_unused]]` suppress `-Wunused` for
variables, parameters, internal functions, and labels.
`__attribute__((deprecated))` and `[[deprecated]]`, with optional string
messages, mark declarations for `-Wdeprecated`.

Any expression reference counts as a symbol use, including assignment targets
and `(void)symbol`. Set-but-not-used analysis is not currently performed.

- Implicit integer-to-narrower-integer conversions use `-Wconversion`
  (e.g. `long` → `int`, `int` → `char`).  Conversions where the source is a
  compile-time constant that fits in the destination are silently accepted.
  Compound-assignment operators (`+=`, `-=`, etc.) on narrower-than-`int`
  lvalues also fire `-Wconversion` because the result is implicitly narrowed
  back to the lvalue type.
- Implicit signed↔unsigned integer conversions use `-Wsign-conversion`
  (e.g. `int x = -1; unsigned int y = x;`).
- Implicit conversions between floating-point and integer types, or from a
  wider float to a narrower float, use `-Wfloat-conversion`
  (e.g. `double d = 3.7; int i = d;`).
- `-Wconversion` is an umbrella: it enables integer narrowing, sign-conversion,
  and float-conversion together.  Each sub-category can also be enabled or
  suppressed independently with `-Wsign-conversion`, `-Wfloat-conversion`,
  `-Wno-sign-conversion`, and `-Wno-float-conversion`.
- `-Werror=conversion` promotes all three conversion sub-categories to errors.
- Comparisons between signed and unsigned integers use `-Wsign-compare`.
  Comparisons where one operand is a non-negative integer constant are exempt
  (e.g. `x < 5` and `x == 0` stay quiet).
- Pointer assignments that discard `const`, `volatile`, or `restrict` qualifiers
  from the pointee type use `-Wdiscarded-qualifiers`.  For example, assigning a
  `const char *` to a `char *` triggers the warning.  Multiple missing qualifiers
  are listed in a single diagnostic.  This warning is part of `-Wall`.
- Arithmetic on `void *` or function pointers (a GNU extension) uses
  `-Wpointer-arith`.  The operation is still performed; only the diagnostic
  is added.
- Use of features that are parsed but have no semantic effect uses
  `-Wignored-features`: `_Atomic` (loads and stores are not guaranteed to be
  atomic across threads — use a mutex for shared mutable state).
- Unknown `__attribute__((...))` and `[[...]]` attributes use `-Wattributes`.
- Discarded return values of functions or types declared with `[[nodiscard]]`
  use `-Wnodiscard`.  An optional message string from `[[nodiscard("...")]]`
  is included in the diagnostic.  Casting to `(void)` suppresses the warning.
  `-Wnodiscard` is part of `-Wall`.
- Unannotated fallthrough between switch case labels uses `-Wfallthrough`.
  The `[[fallthrough]]` null statement attribute marks an intentional fallthrough
  and suppresses the warning for that case group.  `-Wfallthrough` is part of
  `-Wextra`.
- Unknown `#pragma` directives use `-Wcpp`.
- Expression statements whose result has no side effects and is not cast to
  `void` use `-Wunused-value`. Arithmetic, bitwise, logical, comparison,
  conditional, member-access, and dereference subexpressions are considered
  pure. Assignments, function calls, and increment/decrement expressions are
  not, and do not trigger the warning. Casting to `(void)` suppresses it.
- `if` statements whose `then` and `else` bodies are structurally identical use
  `-Wduplicated-branches`. The check uses conservative structural equality of
  the AST: same operation kinds, same variables, same constants. Unrecognised
  constructs (e.g. function calls) are never considered equal.
- Repeated conditions in an `if`/`else if` chain use `-Wduplicated-cond`.
  CCCC walks the full chain and reports each condition that duplicates an
  earlier one in the same chain.
- Multi-character character constants such as `'ab'` use `-Wmultichar`.
  Only the first character contributes to the value; extra characters are
  silently discarded. All five flags are part of `-Wall`.
- Suspicious `main()` signatures use `-Wmain`. Checked at the closing brace
  of the definition: non-`int` return type, parameter count other than 0 or 2,
  first parameter not `int`, and second parameter not `char **` each produce a
  separate diagnostic.
- `switch` statements without a `default:` label use `-Wswitch-default`,
  checked after the full body is parsed.
- `switch` statements whose controlling expression has boolean type (`_Bool`
  or `bool`) use `-Wswitch-bool`. A boolean switch has only two meaningful
  values; an `if`/`else` is usually clearer.
- Direct `==` or `!=` comparisons between two floating-point operands use
  `-Wfloat-equal`. The check fires only when both sides of the operator have
  floating-point type after parsing; mixed integer/float comparisons are not
  flagged.
- Shift expressions where the right-hand operand is a negative integer constant
  use `-Wshift-negative-value`. Shifting by a negative amount is undefined
  behaviour in C.
- Shift expressions where the right-hand operand is a non-negative integer
  constant that equals or exceeds the bit-width of the promoted left-hand type
  use `-Wshift-overflow`. The bit-width accounts for integer promotion (types
  smaller than `int` promote to `int`, i.e. 32 bits).
- `&&` or `||` expressions where one operand is a compile-time constant
  expression (e.g. `x && 1`, `0 || y`) use `-Wlogical-op`. A constant operand
  can never affect the logical result on the side it occupies.
- Comparisons that are always true or always false use `-Wtautological-compare`.
  Two sub-cases are detected: self-comparisons (e.g. `x == x`, `x != x`) where
  the result is determined solely by the operator, and unsigned range checks
  (e.g. `unsigned x >= 0`, `unsigned x < 0`) where the type's value range
  makes the outcome unconditional.
- Calls to `memset`, `memcpy`, `memmove`, or `memcmp` where the size argument
  is `sizeof` applied to a pointer variable or pointer type use
  `-Wsizeof-pointer-memaccess`. This detects a common mistake where the
  programmer writes `sizeof(ptr)` (size of the pointer itself, typically 8
  bytes) instead of `sizeof(*ptr)` or `sizeof(element_type)`.
  All five flags are part of `-Wall`.
- `switch` statements on an enum type that are missing one or more enumerator
  values and have no `default:` label use `-Wswitch`. The set of covered values
  is computed by walking the case labels after the switch body is parsed; GNU
  case ranges (`case 1 ... 5:`) are accounted for. Each missing enumerator
  produces a separate diagnostic. When a `default:` label is present the
  warning is suppressed, because the default arm handles the unhandled values.
  `-Wswitch` is part of `-Wall`.
- `-Wswitch-enum` is the stricter variant of `-Wswitch`: it warns when an enum
  switch is missing enumerator values even when a `default:` label is present.
  This flag is controlled separately from `-Wswitch` so it can be enabled
  independently. It is not part of `-Wall` or `-Wextra`.
- Comparisons (`==`, `!=`, `<`, `<=`, `>`, `>=`) between operands of two
  different named enum types use `-Wenum-compare`. The check fires only when
  both operands have an enum type with a tag name (anonymous enums and
  comparisons against plain integer expressions are not flagged). Named enum
  identity is determined by the enum tag: two uses of the same `enum Foo`
  always refer to the same type and are not flagged. `-Wenum-compare` is part
  of `-Wall`.
- K&R-style (old-style) function definitions — where the parameter names are
  listed in the closing parenthesis and their types are declared in a separate
  list between `)` and `{` (e.g. `int f(x) int x; { }`) — use
  `-Wold-style-definition`. K&R definitions are still accepted and compiled
  correctly; only the diagnostic is added. `-Wold-style-definition` is part of
  `-Wextra`.
- Implicit pointer assignments or function-argument passing where the pointee
  types are structurally incompatible use `-Wincompatible-pointer-types`.
  `void *` on either side is always accepted (C allows implicit `void *`
  conversion); assignments that differ only in qualifiers (e.g. `const int *` →
  `int *`) are covered by `-Wdiscarded-qualifiers` rather than this flag.
  `-Wincompatible-pointer-types` is part of `-Wall`.
- Explicit casts that remove a `const`, `volatile`, or `restrict` qualifier from
  the pointed-to type use `-Wcast-qual` (e.g. `(char *)cstr` where `cstr` is
  `const char *`). The implicit-conversion path is covered by
  `-Wdiscarded-qualifiers`; this flag targets explicit casts only.  Multiple
  discarded qualifiers are listed in a single diagnostic.
- Explicit casts that increase the alignment requirement of the pointer target
  use `-Wcast-align` (e.g. `(int *)char_ptr` on a platform where `int`
  requires 4-byte alignment and `char` requires only 1).  The access through
  the resulting pointer is not necessarily invalid (e.g. when the buffer is
  known to be aligned), but the cast is a common source of undefined behaviour
  on strict-alignment architectures.
- Non-static function definitions without a prior declaration that provides a
  full prototype (explicit parameter types, or `(void)`) use
  `-Wmissing-prototypes`.  The diagnostic fires even when the definition itself
  supplies the prototype; the intent is to catch functions missing from a shared
  header.  Nested functions, `static` functions, and `main` are exempt.  In
  pre-C23 modes an empty `()` declaration does not count as a full prototype.
- Non-static, non-inline function definitions without any prior declaration at
  all use `-Wmissing-declarations`.  A K&R-style or empty-parameter-list
  declaration seen before the definition suppresses this flag (but not
  `-Wmissing-prototypes`).  Nested functions and `main` are exempt.
- A second `extern` variable declaration or a second function forward declaration
  for the same name in the same scope uses `-Wredundant-decls`.  A declaration
  followed by a definition does not trigger this warning.
- A designated initializer that targets a member or array element that was
  already set by an earlier initializer uses `-Woverride-init`.  The later
  value wins (C semantics are preserved); only the warning is issued.
- A `#define` that is never referenced (expanded) in the translation unit uses
  `-Wunused-macros`.  Macros defined in system headers (`<...>` includes from
  the system search path) are excluded.  The warning fires at end-of-file for
  surviving macros and at `#undef` time for macros undef'd before first use.
