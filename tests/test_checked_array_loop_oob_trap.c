// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A loop that runs one past a checked array's extent keeps its range check.

int main(void) {
    int a _Checked[5] = {0};
    int   sum         = 0;
    for (int i = 0; i <= 5; i++)
        sum += a[i];
    return 42;
}
