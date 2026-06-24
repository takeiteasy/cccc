// Test ticket #58: __builtin_dump_tree / __builtin_dump_ast_gen (print + to_string forms).
// The _to_string variants return a string we can inspect at compile time.
// Verified by checking the returned strings contain expected substrings.

#include <string.h>

// Helper: runtime strlen-based substring check (no strstr in CCCC stdlib)
static int contains(const char *hay, const char *needle) {
    int hlen = 0, nlen = 0;
    for (const char *p = hay; *p; p++) hlen++;
    for (const char *p = needle; *p; p++) nlen++;
    for (int i = 0; i <= hlen - nlen; i++) {
        int ok = 1;
        for (int j = 0; j < nlen; j++)
            if (hay[i+j] != needle[j]) { ok = 0; break; }
        if (ok) return 1;
    }
    return 0;
}

// Macro: takes an int literal node, dumps it as tree and as gen,
// returns the original node (the dumps are the side effect we inspect).
[[cccc::comptime]]
Node *dump_and_pass(Node *n) {
    VirtualMachine *vm = __builtin_get_vm();

    // Tree form -- print to stdout
    __builtin_dump_tree(vm, n);

    // Gen form -- print to stdout
    __builtin_dump_ast_gen(vm, n);

    return n;
}

// Macro: returns the tree dump string of the argument node.
[[cccc::comptime]]
Node *tree_string(Node *n) {
    VirtualMachine *vm = __builtin_get_vm();
    const char *s = __builtin_dump_tree_to_string(vm, n);
    // Return the string as a string literal node (for use with contains())
    return __builtin_ast_string_literal(vm, s);
}

// Macro: returns the ast-gen dump string of the argument node.
[[cccc::comptime]]
Node *gen_string(Node *n) {
    VirtualMachine *vm = __builtin_get_vm();
    const char *s = __builtin_dump_ast_gen_to_string(vm, n);
    return __builtin_ast_string_literal(vm, s);
}

int main(void) {
    // dump_and_pass: just verify it doesn't crash and passes the value through
    int x = dump_and_pass(7);
    if (x != 7) return 1;

    // tree_string: the tree dump of an integer literal should mention "NUM"
    const char *ts = tree_string(42);
    if (!contains(ts, "NUM")) return 2;

    // gen_string: the ast-gen dump of an int literal should start with
    // "__builtin_ast_int_literal"
    const char *gs = gen_string(42);
    if (!contains(gs, "__builtin_ast_int_literal")) return 3;

    // gen_string for a binary expression
    const char *bgs = gen_string(1 + 2);
    if (!contains(bgs, "__builtin_ast_binary")) return 4;

    return 42;
}
