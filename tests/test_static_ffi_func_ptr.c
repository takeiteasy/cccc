// Regression test for #589: a static/global initializer that takes the address
// of an extern/FFI function.
//
// apply_global_relocations() used to error ("unsupported relocation to
// undefined function") for any bodyless function referenced from a static
// initializer. SQLite's unix VFS relies on this pattern heavily
// (sqlite3_io_methods / sqlite3_vfs structs of { close, read, write, ... }).
//
// Codegen now stores the FFI dispatch token for such targets, mirroring the
// runtime function-address path, so the pointer both initialises and dispatches
// correctly.

#include <string.h>

typedef struct {
    unsigned long (*len)(const char *);
    int (*cmp)(const char *, const char *);
} Ops;

// Static initializer referencing two FFI functions by address.
static Ops ops = { strlen, strcmp };

int main(void) {
    if (ops.len("hello world!!") != 13)
        return 1;
    if (ops.cmp("abc", "abc") != 0)
        return 2;
    if (ops.cmp("abc", "abd") >= 0)
        return 3;
    return 42;
}
