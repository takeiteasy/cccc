// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: , &__cccc_tmp[0-9]+\)
// CCCC_REJECT_STDOUT: &\(__builtin_memset|&\(__cccc_tmp[0-9]+ =
//
// #1102: taking the address of a block-scope compound literal lowers to
// ND_ADDR over ND_COMMA(memzero+assignments..., hidden temp var), and the
// serializer used to spell that naively as `&(memset(...), t.x = 30, t)` --
// but C's comma operator never yields an lvalue, so every host compiler
// rejected the generated C outright ("cannot take the address of an
// rvalue"). The & now binds to the chain's addressable tail instead:
// `(memset(...), t.x = 30, &t)`. The EXPECT pins the restructured spelling;
// the REJECT fails the test if either old shape ever comes back -- the
// aggregate form (& directly on the memzero chain) or the scalar form (& on
// the leading assignment of an initializer with no memzero).

struct Point {
    int x, y;
};

int main(void) {
    // Aggregate literal: memzero + per-member assignments + temp.
    struct Point *pp = &(struct Point){30, 12};
    // Scalar literal: single assignment + temp, no memzero.
    int *ip = &(int){7};
    return pp->x + pp->y + *ip ^ 41;
}
