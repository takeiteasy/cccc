// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A constant offset before a checked array's start keeps its range check.

int main(void) {
    int a _Checked[5] = {1, 2, 3, 4, 5};
    return *(a - 1);
}
