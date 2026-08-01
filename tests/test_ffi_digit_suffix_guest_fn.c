// Ticket V010 (#876): find_ffi_function's digit-suffix fallback stripped
// trailing digits from an unresolved name and, if a *variadic* FFI
// registration existed under the stripped base name, bound the call to
// that host function instead of the guest's own definition. A guest
// defining its own printf2()/open2()/execl2() (anything colliding with a
// registered variadic base name plus trailing digits) was silently
// rerouted to the host printf/open/execl. The fallback has been removed
// entirely -- this regresion-tests that a guest-defined printf2() is
// actually called.
int printf2(const char *fmt, ...) {
    (void)fmt;
    return 99;
}

int main(void) {
    if (printf2("hello") != 99)
        return 1;
    return 42;
}
