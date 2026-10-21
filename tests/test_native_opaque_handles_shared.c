// @shared opaque-handle idiom: two distinct opaque types next to a comptime
// function.
#include @shared "fixtures/opaque_handles.h"
struct Alpha {
    int tag;
};
struct Beta {
    int tag;
};
Alpha *mk_a(void) {
    static struct Alpha a = {1};
    return &a;
}
Beta *mk_b(void) {
    static struct Beta b = {2};
    return &b;
}
int use_a(Alpha *a) {
    return a->tag == 1 ? 1 : 20;
}
int use_b(Beta *b) {
    return b->tag == 2 ? 2 : 22;
}
[[cccc::comptime]]
Node *check(void) {
    return MakeIntLiteral(0);
}
int main(void) {
    Alpha *a = mk_a();
    Beta  *b = mk_b();
    return use_a(a) + use_b(b) + check() + 39;
}
