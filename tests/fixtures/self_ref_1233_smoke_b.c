#include "self_ref_1233_smoke.h"
struct SelfRef1233 {
    struct SelfRef1233 *next;
    SrFn1233            fn;
    int                 val;
};
static int triple_1233(SelfRef1233 *n) {
    return n->val * 3;
}
SelfRef1233 *make_node_1233_b(int val) {
    static SelfRef1233 n;
    n.next = 0;
    n.fn   = triple_1233;
    n.val  = val;
    return &n;
}
int call_fn_1233_b(SelfRef1233 *n) {
    return n->fn(n);
}
int chain_1233(SelfRef1233 *a, SelfRef1233 *b) {
    a->next = b;
    return a->next->val;
}
