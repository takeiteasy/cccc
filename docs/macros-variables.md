# Comptime Variables

State that persists across comptime calls, and `constexpr` readback.

## Comptime Variables

`[[cccc::comptime]]` can also precede a **variable or struct declaration**. The
value is evaluated during the pre-parse phase and stored so that macros can
read it at compile time.

### Scalar comptime variables

```c
[[cccc::comptime]]
int tile_size = 64;

[[cccc::comptime]]
double pi = 3.14159;

[[cccc::comptime]]
Node *area_of_n_tiles(Node *n) {
    int64_t ts = GetComptimeInt("tile_size");
    return Quote("$$ * $$", n, MakeIntLiteral(ts * ts));
}
```

| API | Returns | Description |
|---|---|---|
| `GetComptimeInt(name)` | `int64_t` | Integer value of a comptime scalar |
| `GetComptimeFloat(name)` | `double` | Float/double value of a comptime scalar |
| `GetComptimeVar(name)` | `Node *` | Comptime scalar as an AST literal node |
| `GetComptimePtr(name)` | `Node *` | Address of a static shadow copy of the evaluated comptime variable |

Use `GetComptimePtr(name)` when generated code needs an addressable value
rather than a literal copy:

```c
[[cccc::comptime]]
int threshold = 42;

[[cccc::comptime]]
Node *threshold_ptr(void) {
    return GetComptimePtr("threshold");
}

int *p = threshold_ptr(); // *p == 42
```

### Struct comptime variables

```c
[[cccc::comptime]]
struct Config { int width; int height; int channels; } cfg = { 1920, 1080, 3 };

[[cccc::comptime]]
Node *pixel_count(void) {
    Node *w = GetComptimeMember("cfg", "width");
    Node *h = GetComptimeMember("cfg", "height");
    return MakeBinary(NK_MUL, w, h);
}
```

Struct and union comptime variables can use tagged, anonymous, or typedef'd
aggregate types. Initializers may be constant expressions or expressions that
call comptime helper functions.

`GetComptimeMember(var_name, field)` returns the field's value as an
AST literal node. Integer and float/double members are supported. Pointer and
array members are not accessible this way.

### Scope notes

- Comptime variables support constant initializers and initializers that call
  comptime helper functions.
- Pointer and string variables produce a compile-time error at this point;
  use `MakeStringLiteral` inside the macro body instead.
- Comptime variables are **not emitted** into the output binary.
- This restriction applies only to comptime *variables* — an actual value that
  needs its own relocation in the macro program's data segment. A `typedef`
  marked `[[cccc::comptime]]` (or written inside a `#pragma cccc comptime
  begin/end` region) declares no object at all, including a function-pointer
  typedef, and is unaffected — see
  [Pre-parse macro declaration context](macros-execution.md#pre-parse-macro-declaration-context).

### constexpr variables

C23 `constexpr` variables can also be read by macros via a separate API.
Unlike `comptime` variables (which are CCCC-specific), `constexpr` follows
standard C23 restricted constant-expression grammar — no function calls,
no variable references in the initializer.

```c
constexpr int BUF_SIZE = 256;
constexpr double SCALE  = 1.5;

[[cccc::comptime]]
Node *make_buf_size(void) {
    return GetConstexprValue("BUF_SIZE");
}
```

| API | Returns | Description |
|---|---|---|
| `GetConstexprValue(name)` | `Node *` | Evaluated initializer of a global `constexpr` variable as an AST literal node (integer or float) |

`GetConstexprValue` errors at compile time if the name does not refer to
a visible global `constexpr` variable.

**`constexpr` vs `comptime`** — these are distinct qualifiers. A `constexpr`
variable is not accessible via `$get_comptime_*` and vice versa. Use
`constexpr` for standard C23 constants; use `comptime` for CCCC-extension values
that can reference comptime functions.
