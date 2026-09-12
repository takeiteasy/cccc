// EXPECT_COMPILE_ERROR
// #486: a dynamic_check() condition must be side-effect-free -- it is only
// evaluated under --checked-pointers, so a side-effecting condition would
// make the two builds diverge, and a future static-elision pass may
// consume the fact without ever evaluating it at all. Same rationale and
// diagnostic style as a count()/byte_count()/bounds() expression.

int main(void) {
    int i = 0;
    __builtin_cccc_dynamic_check(i++ < 5);
    return 0;
}
