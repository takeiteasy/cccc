# Layout and Diagnostic Attributes

`aligned`, `packed`, `unused` and `deprecated`.

## `aligned(N)`

Sets a minimum alignment on a type, variable or member. `aligned` with no argument asks for the maximum useful alignment (16). `[[gnu::aligned(N)]]` is equivalent.

```c
struct __attribute__((aligned(16))) vec4 { float x, y, z, w; };
int __attribute__((aligned(64))) cache_line;

struct Header {
    char a;
    int  b __attribute__((aligned(16)));   // offsetof(Header, b) == 16
};
```

| Rule | Detail |
|---|---|
| Positions | A struct or union body, a global or local variable, and a member (declspec-prefix or declarator-suffix), including a bit-field (`int b : 5 __attribute__((aligned(16)));`) |
| Direction | Only raises alignment; only `packed` lowers it |
| `_Alignas(N)` | An assignment, not a floor. It can raise alignment, but a value below the type's own is a compile error (`'_Alignas' specifiers cannot reduce alignment`). On a bit-field it is rejected outright; use `aligned(N)` |
| Addresses | Honoured for globals, thread-locals and stack locals (including a by-value argument copy), up to 64 bytes; larger values are accepted but placed at 64 |
| `[[gnu::aligned(N)]]` after a struct's closing `}` | Ignored, as GCC does; the `__attribute__` spelling there is honoured |
| `[[...]]` after a bit-field width | Accepted (GCC and clang reject it) |
| `-c=native`, `-m`, `-c=generated` | A type-level or member `aligned`/`_Alignas` is re-emitted only when it widens the layout; a bit-field's alignment is emitted as a trailing GNU attribute, never an `_Alignas` prefix |

`__alignof__(v)` and `_Alignof(v)` on a variable or member give its declared alignment; on any other expression, its type's alignment (including a typedef's `aligned(N)`). A result of a different type reports that type: `__alignof__(g + 1) == 4`, `__alignof__(g + g) == 64`.

## `packed`

Removes the padding between members and at the end of a struct or union.

```c
struct __attribute__((packed)) {
    char c;
    int i;   // directly follows c
};
```

| Rule | Detail |
|---|---|
| Only implicit alignment is removed | An explicit member `aligned(N)` or `_Alignas(N)` still applies and widens that member's offset and the aggregate's alignment. A member whose *type* is `aligned(N)` does not count as explicit |
| Unions | A packed union's alignment is lowered to 1 like a struct's |
| `[[gnu::packed]]` | Same, but not after a definition's closing `}`, where GCC ignores it with a warning |
| Native output | Kept in `-c=native`, `-m` and `-c=generated`, so layouts agree |

`#pragma pack(N)` is different: it caps a following aggregate's alignment at `N` rather than forcing 1, and the cap applies even to an explicit member `aligned(N)` or `_Alignas(N)` request.

## `unused`

Suppresses `-Wunused` on variables, functions, parameters, typedefs and labels. Spellings: `__attribute__((unused))`, `__attribute__((__unused__))`, `[[maybe_unused]]`.

```c
int __attribute__((unused)) x;
int [[maybe_unused]] y;
__attribute__((unused)) static void helper(void) {}
```

## `deprecated`

Warns under `-Wdeprecated` when the identifier is used, with an optional message. Spellings: `__attribute__((deprecated))`, `__attribute__((__deprecated__))`, `[[deprecated]]`.

```c
int __attribute__((deprecated("use bar instead"))) old_func(void);
int [[deprecated]] legacy_var;
```
