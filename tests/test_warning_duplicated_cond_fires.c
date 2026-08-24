// CCCC_FLAGS: -Wduplicated-cond
// CCCC_EXPECT_STDERR: duplicated condition in 'if'/'else if' chain.*\[-Wduplicated-cond\]

int foo(int x) {
    if (x > 0)
        return 1;
    else if (x > 0)
        return 2;
    return 0;
}

int main(void) {
    return foo(1) + 41;
}
