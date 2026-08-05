// #894 fixture: mutually recursive tags, declaration-only and unrouted.
// Resolving A while parsing its splice must not deadlock/recurse forever
// when B (pointer-only, so incomplete-type-safe) refers back to A.
struct A894 {
    struct B894 *b;
    int val;
};

struct B894 {
    struct A894 *a;
};
