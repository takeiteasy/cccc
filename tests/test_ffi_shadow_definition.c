// Ticket V010 (#880): find_ffi_function's exact-match path could bind a
// guest function to the wrong host callee. Unlike #876 (the now-removed
// digit-suffix fallback), this is the exact-name-match path: a guest
// program that defines its own function with a name matching a registered
// FFI symbol -- e.g. `int isatty(int) { ... }` -- had calls to that name
// compiled as CALLF to the *host*'s isatty rather than CALL to the guest's
// own definition, silently calling the wrong code. Fixed by preferring the
// guest's own definition (a function with a body) over FFI resolution;
// a bare declaration (the ordinary libc case) still resolves to FFI.
#include <string.h>
#include <unistd.h>

// Shadows the registered FFI symbol "isatty" with a guest definition.
int isatty(int fd) {
    (void)fd;
    return 77; // not a real isatty() answer -- proves the guest body ran
}

int main(void) {
    if (isatty(0) != 77)
        return 1;

    // strlen has no guest definition here -- an ordinary declaration must
    // still resolve to the host FFI function, not error or misbehave.
    if (strlen("hello") != 5)
        return 2;

    return 42;
}
