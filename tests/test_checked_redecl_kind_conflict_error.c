// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: conflicting checked-pointer bounds for parameter 1 of 'sink'

void sink(int *[[cccc::array]] p);
void sink(int *[[cccc::ntarray]] p);

int main(void) {
    return 42;
}
