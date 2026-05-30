// Test ticket #51: jcc_ast_while / jcc_ast_for / jcc_ast_do_while builders.
// Loop nodes are verified via jcc_dump_ast_gen_to_string since they produce
// statements (not values) and thus can't be tested in expression contexts.
// The _to_string form verifies the nodes are constructed correctly.

#include <reflection.h>
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
#pragma macro
JCC_Node *while_gen_str(JCC_Node *cond, JCC_Node *body) {
    JCC *vm = jcc_get_vm();
    JCC_Node *w = jcc_ast_while(vm, cond, body);
    const char *s = jcc_dump_ast_gen_to_string(vm, w);
    return jcc_ast_string_literal(vm, s);
}

// Macro: builds a for node and returns its gen-dump string.
#pragma macro
JCC_Node *for_gen_str(JCC_Node *init, JCC_Node *cond, JCC_Node *inc, JCC_Node *body) {
    JCC *vm = jcc_get_vm();
    JCC_Node *f = jcc_ast_for(vm, init, cond, inc, body);
    const char *s = jcc_dump_ast_gen_to_string(vm, f);
    return jcc_ast_string_literal(vm, s);
}

// Macro: builds a do-while node and returns its gen-dump string.
#pragma macro
JCC_Node *do_while_gen_str(JCC_Node *body, JCC_Node *cond) {
    JCC *vm = jcc_get_vm();
    JCC_Node *d = jcc_ast_do_while(vm, body, cond);
    const char *s = jcc_dump_ast_gen_to_string(vm, d);
    return jcc_ast_string_literal(vm, s);
}

int main(void) {
    // while: gen dump should mention jcc_ast_while
    int dummy = 0;
    const char *ws = while_gen_str(dummy, dummy);
    if (!contains(ws, "jcc_ast_while")) return 1;

    // for: gen dump should mention jcc_ast_for
    const char *fs = for_gen_str(dummy, dummy, dummy, dummy);
    if (!contains(fs, "jcc_ast_for")) return 2;

    // do-while: gen dump should mention jcc_ast_do_while
    const char *ds = do_while_gen_str(dummy, dummy);
    if (!contains(ds, "jcc_ast_do_while")) return 3;

    return 42;
}
