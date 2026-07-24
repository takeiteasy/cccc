// CCCC_FLAGS: -3
// Ticket #740: a local struct/union member access (t.a) always lowers
// through gen_addr()+emit_load/emit_store, same as a genuine pointer
// dereference, so CHKP3 used to run unconditionally on the computed
// bp+offset address. dead()'s escaping local `s` gets an exact stack_ptr_epochs
// tag; after dead() returns, live()'s non-escaping local `t` can reuse the
// same physical stack address. t.a/t.b are plain ND_MEMBER accesses on a
// local -- never a pointer *value* dereference -- so CHKP3 finding dead's
// stale exact tag there was a false positive. addr_is_local_frame
// (src/codegen.c) now recognizes this and skips CHKP3 entirely for member
// chains rooted at the current frame's own local.
typedef struct {
    long a;
    long b;
    long c;
} S;

void use(S *p) { p->a = 1; }

static int dead(void) {
    S s = {0, 0, 0};
    use(&s);
    return (int)s.a;
}

static int live(void) {
    S t;
    t.a = 5;
    t.b = 6;
    return (int)(t.a + t.b);
}

int main(void) {
    if (dead() != 1) return 1;
    if (live() != 11) return 2;
    return 42;
}
