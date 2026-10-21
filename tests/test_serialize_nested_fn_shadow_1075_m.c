// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __cccc_dup
// -m output renames the nested function that collides with the outer one.
int add(int a, int b) {
    return a + b;
}
int uses_outer_add(void) {
    return add(10, 5);
}
int main(void) {
    int add(int a, int b) {
        return a + b + 1;
    }
    if (add(40, 1) != 42)
        return 1;
    if (uses_outer_add() != 15)
        return 2;
    if (add(40, 2) != 43)
        return 3;
    return 42;
}
