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

- `#warning` uses `-Wcpp`.
- Extra preprocessor tokens use `-Wextra-tokens`.
- Large `#embed` files use `-Wlarge-file-embed`.
- `__jcc_macro_warning_at` uses `-Wjcc-macro`.

The semantic categories are reserved for parser/type/codegen diagnostics and
are available for command-line compatibility. Their checks are implemented in
smaller follow-up tasks.
