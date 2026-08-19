// CCCC_FLAGS: --testing
//
// Ticket #1078: the VM's own calling convention passed a struct/union
// by-value parameter as a raw pointer to the CALLER's own object, with no
// copy -- a write through the parameter inside the callee silently mutated
// the caller's argument. Every real host C compiler, and -c=native, copies
// the argument; only the VM's own convention didn't. Confirmed against a
// pre-fix binary: every one of this file's write-through cases returned a
// mutated caller value instead of leaving it untouched.
//
// This was also the actual root cause behind #1062's inverted premise (a
// va_list forwarded as a function parameter): CCCC's own va_list is a
// struct, and the VM's aliasing bug made it behave like glibc's array-decay
// va_list, not macOS's genuine by-value one, exactly backwards from what
// #1062 assumed.
//
// Fixed in the callee's own prologue (gen_function, src/codegen_func.c):
// each struct/union parameter is copied into a fresh frame-local scratch
// slot and the parameter's slot is rebound to point at the copy instead of
// the caller's object.

// ---------------------------------------------------------------------
// Basic struct write-through: the caller's object must be untouched.
// ---------------------------------------------------------------------

struct point { long x; long y; };

static void mutate_point(struct point p) {
    p.x = 999;
    p.y = 999;
}

[[cccc::test]]
void test_struct_byval_write_no_alias(void) {
    struct point p = {1, 2};
    mutate_point(p);
    AssertEq(p.x, 1);
    AssertEq(p.y, 2);
}

// ---------------------------------------------------------------------
// Union write-through.
// ---------------------------------------------------------------------

union pun { long a; double d; };

static void mutate_union(union pun u) {
    u.a = 12345;
}

[[cccc::test]]
void test_union_byval_write_no_alias(void) {
    union pun u;
    u.a = 7;
    mutate_union(u);
    AssertEq(u.a, 7);
}

// ---------------------------------------------------------------------
// Nested-member and array-member writes.
// ---------------------------------------------------------------------

struct inner { int v; };
struct outer { struct inner in; int arr[4]; };

static void mutate_outer(struct outer o) {
    o.in.v = -1;
    o.arr[2] = -1;
}

[[cccc::test]]
void test_struct_byval_nested_member_write_no_alias(void) {
    struct outer o = {{5}, {10, 11, 12, 13}};
    mutate_outer(o);
    AssertEq(o.in.v, 5);
    AssertEq(o.arr[2], 12);
}

// ---------------------------------------------------------------------
// &param taken inside the callee -- must point at the callee's own copy,
// not the caller's object, and any write through it must not alias either.
// ---------------------------------------------------------------------

static void mutate_via_address(struct point p) {
    struct point *pp = &p;
    pp->x = 42;
}

[[cccc::test]]
void test_struct_byval_addr_of_param_no_alias(void) {
    struct point p = {1, 2};
    mutate_via_address(p);
    AssertEq(p.x, 1);
}

// ---------------------------------------------------------------------
// Large (>64-byte) struct.
// ---------------------------------------------------------------------

struct big { long words[16]; }; // 128 bytes

static void mutate_big(struct big b) {
    for (int i = 0; i < 16; i++)
        b.words[i] = -1;
}

[[cccc::test]]
void test_struct_byval_large_no_alias(void) {
    struct big b;
    for (int i = 0; i < 16; i++)
        b.words[i] = i;
    mutate_big(b);
    for (int i = 0; i < 16; i++)
        AssertEq(b.words[i], (long long)i);
}

// ---------------------------------------------------------------------
// Recursion: each activation must get its own copy.
// ---------------------------------------------------------------------

static long sum_via_copy(struct point p, int depth) {
    p.x += depth; // mutate this frame's own copy only
    if (depth == 0)
        return p.x;
    return p.x + sum_via_copy(p, depth - 1);
}

[[cccc::test]]
void test_struct_byval_recursion_own_copy(void) {
    struct point p = {0, 0};
    // depth=3: contributions are (0+3)+(0+3+2)+(0+3+2+1)+(0+3+2+1+0)
    //        = 3 + 5 + 6 + 6 = 20
    long total = sum_via_copy(p, 3);
    AssertEq(total, 20LL);
    // caller's own p must be untouched by any recursive frame's writes.
    AssertEq(p.x, 0);
}

// ---------------------------------------------------------------------
// Nested function: a struct param owned by a nested function must also get
// its own copy (nested functions compile through the same gen_function
// prologue as top-level ones).
// ---------------------------------------------------------------------------

[[cccc::test]]
void test_struct_byval_nested_function_no_alias(void) {
    struct point p = {1, 2};
    void mutate_nested(struct point q) {
        q.x = 500;
    }
    mutate_nested(p);
    AssertEq(p.x, 1);
}
