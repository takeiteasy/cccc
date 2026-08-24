// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: , &__cccc_tmp[0-9]+(\.x|\.y)?\).*&\(\*__cccc_tmp[0-9]+\)\.y
// CCCC_REJECT_STDOUT: &\(__builtin_memset|&\(__cccc_tmp[0-9]+ =|\)\.x;|\*__cccc_tmp[0-9]+\.y
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
//
// #1102 followup: the addressed expression can also carry a postfix shell
// above the chain -- postfix binds tighter than unary &, so in
// `&((struct Point){40, 41}).x` the `.x` is part of the addressed lvalue,
// and a pointer-typed literal's `->y` layers an explicit deref under the
// member access. Both used to emit the equally-invalid `&(..., t).x`; both
// now bind the & inside the chain while covering the whole shell:
// `(..., &t.x)` and `(..., &(*t).y)`. The REJECT's `\)\.x;` pins the old
// shell-outside spelling away, and `\*t.y` would catch a regression to
// unparenthesized deref-under-member (which re-parses as *(t.y)).

struct Point {
    int x, y;
};

int main(void) {
    // Aggregate literal: memzero + per-member assignments + temp.
    struct Point *pp = &(struct Point){30, 12};
    // Scalar literal: single assignment + temp, no memzero.
    int *ip = &(int){7};
    // Member access through a parenthesized literal.
    int *mp = &((struct Point){40, 41}).x;
    // Arrow through a pointer-typed literal: MEMBER(DEREF(comma)).
    struct Point local = {50, 51};
    int         *ma    = &((struct Point *){&local})->y;
    return pp->x + pp->y + *ip + *mp + *ma ^ 41;
}
