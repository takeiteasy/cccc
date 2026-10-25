# JSON Derive

Generate JSON encoders and decoders for a struct.

## JSON

`@to_json` and `@from_json` also need `#include <cccc/json.h>`, the runtime the
generated functions call (a missing include is a compile error that names it).

```c
#include <cccc/derive.h>
#include <cccc/json.h>

@to_json
@from_json
typedef struct { int id; char name[16]; double scores[2]; } User;

User  u = {7, "ann", {1.5, 2}};
char  buf[128];
size_t n = User_to_json(&u, buf, sizeof buf);
// buf: {"id":7,"name":"ann","scores":[1.5,2]}

User v;
if (User_from_json(&v, buf) != 0) { /* malformed or mismatched */ }
```

`T_to_json` returns the length the whole output needs, like `snprintf`: the
buffer is always NUL-terminated when `cap > 0`, and `cap == 0` only measures.
`T_from_json` returns 0 on success and nonzero otherwise.

| Member | JSON |
|---|---|
| Integers, enums | Number (an enum is its numeric value) |
| `float`, `double` | Number; NaN and infinities are written as `null` |
| `bool` | `true` / `false` |
| `char[N]` | String, escaped; reading fails if it does not fit |
| `T[N]` | Array; reading requires exactly `N` elements |
| Struct, anonymous struct | Object, expanded in place |
| `char *s @owned` | String, or `null` for `NULL` |
| `T *a @owned("n")` | Array of `n` elements, or `null` |
| `T *p @owned` | The value of `*p`, or `null` |

A pointer that is not `@owned`, a union member, or a union target is a compile
error that names the member (`Outer.inner.name`).

Reading is strict: every member must be present with the right type, a number
must fit the member (`256` does not fit an `unsigned char`), and anything after
the closing `}` is an error. Members the struct does not have are skipped, in
any order. Doubles are written with 17 significant digits, so `0.1` comes back
as `0.10000000000000001`.

A prefix names the functions `<prefix>_<Type>_to_json` and
`<prefix>_<Type>_from_json`: `@to_json("app")` on `struct Pair` publishes
`app_Pair_to_json`.

### Owned members

An `@owned` member (see [Owned memory](derive-owned.md#owned-memory)) is written as its
content, and `T_from_json` allocates it with `malloc`:

```c
@destroy @to_json @from_json
struct Doc {
    char *title @owned;
    int   n;
    int  *words @owned("n");
};
// {"title":"notes","n":2,"words":[3,4]}

struct Doc d;
if (Doc_from_json(&d, json) == 0) {
    /* use d */
    Doc_destroy(&d);
}
```

`T_from_json` fills `*out` without freeing what it held, so pass a struct that
owns nothing. On failure it releases everything it allocated and leaves `*out`
untouched. An array's length is the number of elements read: the length member
is still written and read under its own key, and a key that disagrees with the
array fails the read. A repeated key for an owned member fails too, and two
owned arrays cannot share one length member.

A struct pointee that owns memory, or is the struct itself (a linked list), is
written and read through its own `@to_json` / `@from_json`, so put those
attributes on the pointee first. A struct that owns memory also needs
`@destroy` before `@from_json`, which releases a failed read through it. A
pointee that is only forward-declared can be read by `@from_json` if it also
has `@clone`, which gives the reader its size; `@to_json` rejects it.[^json]

### Skipping and renaming members

`@json_skip` and `@json_name("key")` go on the members, in any declarator
position (`int id @json_name("user_id");`, `@json_skip int a, b;`,
`unsigned flag : 1 @json_skip;`):

```c
@to_json
@from_json
struct Account {
    int  id @json_name("account_id");
    char owner[8];
    int  secret @json_skip;
};
// {"account_id":7,"owner":"bob"}
```

A skipped member is not written, and `T_from_json` leaves it as it was. Its
key in the input is ignored like any other unknown key. A renamed member is
read and written under the new key only. Two members with the same key
(after renaming) are a compile error, as is `@json_skip` together with
`@json_name`, a key that is empty or longer than 127 bytes, or either attribute
outside a struct member.
