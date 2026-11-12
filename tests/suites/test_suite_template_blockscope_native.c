// CCCC_FLAGS: --testing
// CCCC_NATIVE_SKIP: -c=native writes a block-scope template argument inline in the file-scope instance, so a pointer to it has the wrong type (#2024)
// Suite: a block-scope struct as a template argument, through pointers and
// nesting.

#include <stddef.h>

@template(T) struct Vec {
    T     *data;
    size_t len, cap;
};
@template(A, B) struct Pair {
    A first;
    B second;
};

static int block_scope_pointer_and_nesting(void) {
    struct Ambient {
        short x[3];
    };
    @inst(struct Ambient, int) Pair                  inner = {{{1, 2, 3}}, 4};
    @inst(@inst(struct Ambient, int) Pair, int) Pair outer = {inner, 5};
    struct Ambient                                  *pa    = &inner.first;
    @inst(struct Ambient *) Vec                      v     = {&pa, 1};
    return (int)sizeof outer.first.first + v.data[0]->x[2] + outer.second;
}

[[cccc::test]]
void test_block_scope_aggregates_in_pointers_and_nesting(void) {
    AssertEq(block_scope_pointer_and_nesting(), 6 + 3 + 5);
}
