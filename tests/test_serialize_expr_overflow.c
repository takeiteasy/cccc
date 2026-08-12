// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __builtin_add_overflow\(
// CCCC_REJECT_STDOUT: unsupported expr kind
//
// #964: ND_OVERFLOW_ARITH had no serializer case -- it hit the default arm
// and printed `/* unsupported expr kind 55 */` in place of the checked
// arithmetic call. val (0/1/2, set in parse.c) selects add/sub/mul; this
// maps directly back onto the same-named clang/gcc builtin the parser
// accepted in the first place.

int main(void) {
    int r;
    int ok = __builtin_add_overflow(2, 3, &r);
    return r + ok;
}
