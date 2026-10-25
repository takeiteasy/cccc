// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A global `_Checked[]` takes its extent from the initializer.

int a _Checked[] = {1, 2, 3};

int main(void) {
    volatile int i = 3;
    return a[i];
}
