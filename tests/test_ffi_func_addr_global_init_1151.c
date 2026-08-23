// Ticket #1151: taking a libc function's address in a global initializer
// (e.g. a static vtable naming `strlen`/`strcmp` directly) used to make
// -c=native's #999 reloc-forward-declare loop (serialize_program.c) emit a
// second, conflicting prototype for that libc function -- CCCC's own
// bundled string.h spelling (`long strlen(const char *s);`), colliding
// with the host's real `size_t strlen(const char *)` once the replayed
// `#include <string.h>` brought it into scope ("conflicting types for
// 'strlen'"). Regression-tests the fix: the reloc loop now skips a target
// whose declaration is already header-supplied, same as the ordinary
// function-prototype pass already did.
#include <string.h>

typedef struct {
    unsigned long (*len)(const char *);
    int (*cmp)(const char *, const char *);
} FfiOps;

static FfiOps ffi_ops_global = {strlen, strcmp};

int main(void) {
    if (ffi_ops_global.len("hello world!!") != 13)
        return 1;
    if (ffi_ops_global.cmp("abc", "abc") != 0)
        return 2;
    if (ffi_ops_global.cmp("abc", "abd") >= 0)
        return 3;
    return 42;
}
