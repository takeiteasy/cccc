// Test ticket #51: __cccc_ast_while / __cccc_ast_for / __cccc_ast_do_while builders.
// Loop nodes are verified via __cccc_dump_ast_gen_to_string since they produce
// statements (not values) and thus can't be tested in expression contexts.
// The _to_string form verifies the nodes are constructed correctly.

#include <string.h>

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

// Macro: builds a while node and returns its gen-dump string.
[[cccc::comptime(inline)]]
$node_t *while_gen_str($node_t *cond, $node_t *body) {
    $vm_t *vm = __cccc_get_vm();
    $node_t *w = __cccc_ast_while(vm, cond, body);
    const char *s = __cccc_dump_ast_gen_to_string(vm, w);
    return __cccc_ast_string_literal(vm, s);
}

// Macro: builds a for node and returns its gen-dump string.
[[cccc::comptime(inline)]]
$node_t *for_gen_str($node_t *init, $node_t *cond, $node_t *inc, $node_t *body) {
    $vm_t *vm = __cccc_get_vm();
    $node_t *f = __cccc_ast_for(vm, init, cond, inc, body);
    const char *s = __cccc_dump_ast_gen_to_string(vm, f);
    return __cccc_ast_string_literal(vm, s);
}

// Macro: builds a do-while node and returns its gen-dump string.
[[cccc::comptime(inline)]]
$node_t *do_while_gen_str($node_t *body, $node_t *cond) {
    $vm_t *vm = __cccc_get_vm();
    $node_t *d = __cccc_ast_do_while(vm, body, cond);
    const char *s = __cccc_dump_ast_gen_to_string(vm, d);
    return __cccc_ast_string_literal(vm, s);
}

int main(void) {
    // while: gen dump should mention __cccc_ast_while
    int dummy = 0;
    const char *ws = while_gen_str(dummy, dummy);
    if (!contains(ws, "__cccc_ast_while")) return 1;

    // for: gen dump should mention __cccc_ast_for
    const char *fs = for_gen_str(dummy, dummy, dummy, dummy);
    if (!contains(fs, "__cccc_ast_for")) return 2;

    // do-while: gen dump should mention __cccc_ast_do_while
    const char *ds = do_while_gen_str(dummy, dummy);
    if (!contains(ds, "__cccc_ast_do_while")) return 3;

    return 42;
}
