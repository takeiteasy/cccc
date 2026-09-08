// Regression test for #1329's own follow-up: a trailing __attribute__ after
// a declarator's asm-label was rejected ("expected '{'"), even though the
// reverse order (attribute then asm-label) already worked. Found verifying
// #1329 on the cccc-linux-amd64 Colima container -- real glibc's
// <sys/cdefs.h> __REDIRECT_NTH macro (reached via <string.h>'s strerror_r,
// pulled in transitively by the comptime reflection pass on any
// __attribute__-bearing declaration under --use-system-headers) expands to
// exactly this shape: `name proto __asm__ ("realname") __THROW`, i.e. the
// asm-label comes BEFORE the trailing attribute, not after. declarator()
// (src/parse_types.c) only ever looked for one attribute-list pass followed
// by one asm_label() call -- an attribute list AFTER the asm-label was never
// consumed, and the leftover "__attribute__" token where a ';' or '{' was
// expected produced the misleading "expected '{'" diagnostic. Fixed by
// looping attribute-list/asm-label parsing until a pass consumes nothing, so
// any interleaving is accepted.
extern int foo(void) __asm__("foo") __attribute__((__nothrow__));

// The double-attribute, multi-argument shape __REDIRECT_NTH actually
// produces (asm-label, then TWO chained attribute-lists, one of them
// carrying arguments) -- the exact real-world trigger, not just the
// minimal one-attribute case above.
extern int bar(int a, char *buf, unsigned long len) __asm__("bar")
    __attribute__((__nothrow__)) __attribute__((__nonnull__(2)));

int foo(void) {
    return 42;
}
int bar(int a, char *buf, unsigned long len) {
    (void)a;
    (void)buf;
    (void)len;
    return 42;
}

int main(void) {
    if (bar(1, (char *)"x", 1) != 42)
        return 1;
    return foo();
}
