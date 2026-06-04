# Warning Options

JCC supports gcc/clang-style warning category controls for suppressible
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

## Supported Names

The warning infrastructure recognizes these category names:

- `unused`
- `implicit-function-declaration`
- `implicit-int`
- `return-type`
- `shadow`
- `format`
- `conversion`
- `sign-compare`
- `pointer-arith`
- `pedantic`
- `deprecated`
- `cpp`
- `extra-tokens`
- `large-file-embed`
- `jcc-macro`

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
  `-Wreturn-type`. JCC inserts a correctly typed zero return where needed.
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
- `#warning` uses `-Wcpp`.
- Extra preprocessor tokens use `-Wextra-tokens`.
- Large `#embed` files use `-Wlarge-file-embed`.
- `__jcc_macro_warning_at` uses `-Wjcc-macro`.

Implicit functions that remain unresolved are hard errors during code
generation. Missing return values for struct and union functions are also hard
errors because JCC cannot synthesize a safe aggregate value.

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

The remaining semantic categories are available for command-line compatibility
and are implemented in smaller follow-up tasks.
