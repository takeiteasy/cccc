// Test ticket #51: __builtin_ast_while / __builtin_ast_for /
// __builtin_ast_do_while builders. Loop nodes are verified via
// __builtin_dump_ast_gen_to_string since they produce statements (not values)
// and thus can't be tested in expression contexts. The _to_string form verifies
// the nodes are constructed correctly.

#include <string.h>

static int contains(const char *hay, const char *needle) {
    int hlen = 0, nlen = 0;
    for (const char *p = hay; *p; p++)
        hlen++;
    for (const char *p = needle; *p; p++)
        nlen++;
    for (int i = 0; i <= hlen - nlen; i++) {
        int ok = 1;
        for (int j = 0; j < nlen; j++)
            if (hay[i + j] != needle[j]) {
                ok = 0;
                break;
            }
        if (ok)
            return 1;
    }
    return 0;
}

// Macro: builds a while node and returns its gen-dump string.
[[cccc::comptime]]
Node *while_gen_str(Node *cond, Node *body) {
    Node       *w = __builtin_ast_while(cond, body);
    const char *s = __builtin_dump_ast_gen_to_string(w);
    return __builtin_ast_string_literal(s);
}

// Macro: builds a for node and returns its gen-dump string.
[[cccc::comptime]]
Node *for_gen_str(Node *init, Node *cond, Node *inc, Node *body) {
    Node       *f = __builtin_ast_for(init, cond, inc, body);
    const char *s = __builtin_dump_ast_gen_to_string(f);
    return __builtin_ast_string_literal(s);
}

// Macro: builds a do-while node and returns its gen-dump string.
[[cccc::comptime]]
Node *do_while_gen_str(Node *body, Node *cond) {
    Node       *d = __builtin_ast_do_while(body, cond);
    const char *s = __builtin_dump_ast_gen_to_string(d);
    return __builtin_ast_string_literal(s);
}

int main(void) {
    // while: gen dump should mention __builtin_ast_while
    int         dummy = 0;
    const char *ws    = while_gen_str(dummy, dummy);
    if (!contains(ws, "__builtin_ast_while"))
        return 1;

    // for: gen dump should mention __builtin_ast_for
    const char *fs = for_gen_str(dummy, dummy, dummy, dummy);
    if (!contains(fs, "__builtin_ast_for"))
        return 2;

    // do-while: gen dump should mention __builtin_ast_do_while
    const char *ds = do_while_gen_str(dummy, dummy);
    if (!contains(ds, "__builtin_ast_do_while"))
        return 3;

    return 42;
}
