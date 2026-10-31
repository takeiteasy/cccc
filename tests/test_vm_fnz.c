// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: : FNZ3 +[0-9]+
// CCCC_EXPECT_STDOUT: : FNZ3_F32 +[0-9]+
// A float or double used as a truth value is tested against zero in its own
// register with FNZ3, not by the integer branch ops.
int dbl(double d) {
    if (d)
        return 1;
    return 0;
}

int flt(float f) {
    return !f;
}

int main(void) {
    return dbl(0.5) + flt(0.0f) == 2 ? 42 : 1;
}
