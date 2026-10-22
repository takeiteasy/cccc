# Native Output and `--std=`

The emitted C is always **GNU C11**; `--std=` only decides which `-std=` spelling is forwarded to the host.

The front end accepts C23, and the serializer lowers everything past C11:

| C23 construct | Emitted as |
|---|---|
| `auto` | The resolved type |
| One-argument `static_assert` | Two-argument `_Static_assert` |
| `nullptr` | `void *` |
| `_BitInt(N)` | A fixed integer container |
| `[[gnu::...]]` | `__attribute__` |
| Digit separators, binary literals | Folded away |

## Spelling probe

| Your `--std=` | Tried, in order |
|---|---|
| C11 or later, e.g. `c23` | Only that standard: `c23`/`gnu23`/`c2x`/`gnu2x` |
| Older (`c99`, `c89`, `gnu` forms) | C11's spellings (`--std=c99` forwards `-std=gnu11`) |
| None | The newest rung the host accepts, descending to C11; nothing if none |

- `gnuNN` is tried before strict `cNN`, because the output uses GNU constructs (statement expressions, `__int128`, `case A ... B`, `__asm__`) that strict mode rejects.
- If an explicit `--std=` has no accepted spelling, `-c=native` fails naming the compiler and the spellings tried.
- With no `--std=` and no usable spelling, the host's default dialect is checked against the C11 floor, and `-c=native` fails if it cannot parse the output.
