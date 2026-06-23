// Regression test for #588: a VLA combined with __attribute__((constructor)).
//
// The constructor attribute registers the implicit macro
// __builtin_attr_generate_constructor, which makes cc_expand_macros re-run
// parse(). That re-parse reassigned vm->compiler.builtin_alloca to a fresh Obj,
// so the VLA's alloca callee node (built during the main parse) no longer
// matched the pointer codegen compared against, fell into the indirect-call
// path, and dereferenced a NULL node->ty -> host SIGSEGV in gen_addr.
//
// Codegen now identifies the builtin alloca by a stable is_builtin_alloca flag,
// so this compiles and runs correctly.

__attribute__((constructor)) static void init_thing(void) {}

static int sink(char *p, int n) {
    p[0] = (char)n; // touch the VLA storage
    return p[0];
}

int main(void) {
    int n = 8;
    char buf[n + 4];
    if ((int)sizeof(buf) != 12)
        return 1;
    return sink(buf, 42);
}
