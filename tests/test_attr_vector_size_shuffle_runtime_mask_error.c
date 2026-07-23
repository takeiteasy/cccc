// EXPECT_COMPILE_ERROR
// GNU __builtin_shuffle (tracker #715): a runtime/named vector mask is not
// yet supported -- CCCC only implements the compile-time-constant
// brace-list mask form (see COVERAGE.md and the wider (256/512-bit)/runtime-
// shuffle follow-up ticket).

typedef int v4si __attribute__((vector_size(16)));

int main(void) {
    v4si a = {1, 2, 3, 4};
    v4si mask = {3, 2, 1, 0};
    v4si sh = __builtin_shuffle(a, mask); // error: mask must be a brace-enclosed constant list
    return sh[0] == 4 ? 42 : 1;
}
