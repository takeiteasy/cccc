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

## Pragma-Based Suppression

Within source files, `#pragma GCC diagnostic` (also accepted as `#pragma clang
diagnostic` or `#pragma CCCC diagnostic`) controls warning state inline:

```c
#pragma GCC diagnostic push           // save current warning state
#pragma GCC diagnostic pop            // restore saved state
#pragma GCC diagnostic ignored "-Wunused"   // suppress a category
#pragma GCC diagnostic warning "-Wunused"   // enable a category as warning
#pragma GCC diagnostic error   "-Wunused"   // promote a category to error
```

`push` and `pop` nest: each `push` saves the current state onto a stack, and
the matching `pop` restores it. Unmatched `pop` emits a `-Wcpp` diagnostic.

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
- Arithmetic on `void *` or function pointers (a GNU extension) uses
  `-Wpointer-arith`.  The operation is still performed; only the diagnostic
  is added.
- Use of features that are parsed but have no semantic effect uses
  `-Wignored-features`: `_Atomic` (loads and stores are not atomic),
  `_Thread_local` / `__thread` / `thread_local` (no thread-local storage is
  provided).
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
