# `format`

`__attribute__((format(printf|scanf, fmt_index, first_arg)))` makes calls type-check their format strings at compile time. It needs `--format-string-checks` (part of `-1` and above), and the `printf`/`scanf` families in the bundled headers are already annotated ([safety-pointers.md](safety-pointers.md#--format-string-checks)).

| Archetype | Checked |
|---|---|
| `printf`, `gnu_printf`, `printf0` | Yes |
| `scanf`, `gnu_scanf` | Yes |
| `strftime`, `strfmon`, `os_log`, `os_trace`, `NSString`, `CFString`, `kprintf`, `syslog`, `ms_*` | Accepted, not validated |
| Anything else | `-Wattributes` warning, not validated |

Each archetype also accepts the `__x__` form.

## Length modifiers

Argument types are validated with the length modifier:

| Specifier | Expected |
|---|---|
| `%ld` | `long` |
| `%zu` | `unsigned long` |
| `%Lf` | `long double` |
| `%Ld %Li %Lu %Lo %Lx` | `L` is read as `ll` (GNU pre-C99 spelling): `long` or `unsigned long`, as glibc and gcc/clang `-Wformat` do. The runtime formatter does the same in `printf` and `scanf` |
| `%hhn` | `char *` |
| `%hn` | `short *` |
| `%ln %lln %Ln %jn %zn %tn` | An 8-byte integer pointer of either signedness (`%n` has no signed/unsigned split) |
| Bare `%n` | Lenient |

The runtime `%n` store follows the modifier's width, writing exactly a `signed char`, `short`, `long`, `long long`, `intmax_t`, `size_t` or `ptrdiff_t`.

A `long double` passed to a plain `%f`, `%e` or `%g` (no `L`) is a width mismatch, not a promotion, because variadic `long double` is marshalled as a real `long double` and the argument frame would otherwise desync. The `%L` conversions are performed at `double` precision, like all `long double` arithmetic.
