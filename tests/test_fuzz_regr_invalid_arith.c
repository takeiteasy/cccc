// EXPECT_COMPILE_ERROR
// Regression test for ticket #143: new_add/new_sub NULL dereference
// on invalid pointer arithmetic. Before the fix, this crashed with
// a SEGV in parse.c:3040 (lhs->ty->base->kind on NULL base).

int main() {
    int b = 10 - &;
    return 42;
}
