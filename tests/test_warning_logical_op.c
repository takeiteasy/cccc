// CCCC_FLAGS: -Wlogical-op
// CCCC_EXPECT_STDERR: right operand of '&&' is a constant
// expression.*\[-Wlogical-op\]

int main(void) {
    int x = 1;
    if (x && 1)
        x = 2;
    return 42;
}
