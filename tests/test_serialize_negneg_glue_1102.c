// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: \? -\(-5\) : .*= -\(-v\);
// CCCC_REJECT_STDOUT: --5|--v
//
// #1102: `-(-5)` -- ND_NEG over ND_NEG, typically from a macro like
// `#define abs(x) ((x) < 0 ? -(x) : (x))` expanded on -5 -- used to
// serialize flat as `--5`, which a real compiler re-lexes as pre-decrement
// of a literal ("expression is not assignable"). The inner operand now
// keeps its parentheses whenever a '-' would otherwise end up glued to
// another '-', including when the nested negation hides behind an implicit
// widening cast that serializes as nothing (_Generic-selected arms are
// exactly that shape -- see test_suite_std_c11.c's myabs). The REJECT
// guards both spellings; this test deliberately contains no real
// pre/post-decrement, so any '--' token in the output is a regression.

#define ABS(x) ((x) < 0 ? -(x) : (x))

int main(void) {
    int v = ABS(-5);
    int w = -(-v);
    return v == 5 && w == 5 ? 42 : 1;
}
