// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int \(abs\)\(int
// -m output for a bodiless declaration keeps its real parameter type.
int abs(int x);
int main(void) {
    return abs(-1) + 41;
}
