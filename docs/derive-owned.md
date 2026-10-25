# Owned Memory

`@clone` and `@destroy` copy and free the pointer members marked `@owned`.

## Owned memory

`@clone` and `@destroy` copy and free the pointer members you mark `@owned`.
Unmarked members are copied as they are, so a plain pointer is shared.
`#include <stdlib.h>` is required.

```c
#include <cccc/derive.h>
#include <stdlib.h>

@clone @destroy
struct Doc {
    char *title @owned;          // heap C string
    int   n;
    int  *words @owned("n");     // heap array of n ints
    char *cursor;                // borrowed, copied as a pointer
};

struct Doc copy;
if (Doc_clone(&copy, &doc) == 0) {
    /* copy owns its own title and words */
    Doc_destroy(&copy);
}
```

| Member | `@clone` | `@destroy` |
|---|---|---|
| `char *s @owned` | Copies the string | `free`s it |
| `T *p @owned` | Copies one `T` | `free`s it |
| `T *a @owned("n")` | Copies `n` elements, `n` an integer member of the same struct | `free`s the array |

`T_clone` returns 0 on success. If an allocation fails it frees what it had
already copied, returns 1 and leaves `*out` untouched. A NULL member stays
NULL. `T_destroy` sets each freed member to NULL, so calling it twice is
harmless, and does not free `self`.

When the pointee is itself a struct that owns memory, `T_clone` calls the
pointee's `_clone` and `_destroy`, so put `@clone @destroy` on the pointee
first. A struct can own pointers to itself (a linked list). Nested and
anonymous structs, and arrays of them, are walked in place. An `@owned` member
whose pointee needs a missing function is a compile error that names it
(`needs Inner_clone`).

A pointee that is only forward-declared can be declared after its owner, which
lets two structs own pointers to each other. The forward-declared struct needs
`@clone`, which gives the other derives its size for an `@owned("n")` array:

```c
struct Leaf;

@clone @destroy struct Branch {
    struct Leaf *leaves @owned("n");
    int          n;
};

@clone @destroy struct Leaf {
    struct Branch *up @owned;
};
```

The pointee must get `@clone` and `@destroy` later in the program; a missing
one is an undefined-function error when the program links.[^size]

`@clone` needs `@destroy` on the same type. `@clone("app")` and
`@destroy("app")` publish `app_Doc_clone` and `app_Doc_destroy`, and the same
prefix is used to find a pointee's functions. `@owned` goes on pointer
members only; a `void *` member or a pointee with `const` elements cannot
be cloned.
