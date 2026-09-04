// Expected return: 42
// #1282: `f().member` on a function returning a struct/union by value --
// legal C (6.5.2.3p3: a function-call result is not an lvalue, but member
// access on it is still permitted) that cccc's own gen_addr rejected with
// "not an lvalue", since its switch had no case for ND_FUNCALL. A
// struct/union-returning call already gets a caller-allocated ret_buffer
// (parse_postfix.c's funcall()), and gen_expr's own ND_FUNCALL case already
// yields that buffer's ADDRESS for a struct/union return -- the same
// by-reference convention every aggregate value uses -- so gen_addr just
// needed to run the call and reuse that address. Surfaced by the
// self-hosting spike (src/macros.c: `comptime_aggregate_cast(cv).kind`).
struct Pair {
    int a;
    int b;
};

struct Pair make(int x) {
    struct Pair p = {x, x + 1};
    return p;
}

int main(void) {
    if (make(5).a != 5)
        return 1;
    if (make(5).b != 6)
        return 2;

    // Two separate calls used in the same expression must not alias each
    // other's return buffer (RETBUF rotation).
    int sum = make(10).a + make(20).b;
    if (sum != 31)
        return 3;

    // Member access as the target of a compound expression (read-modify),
    // not just a bare read.
    struct Pair q = {0, 0};
    q.a           = make(7).b;
    if (q.a != 8)
        return 4;

    return 42;
}
