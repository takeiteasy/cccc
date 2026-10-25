// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// `_Checked[]` takes its extent from the initializer, so one past the last
// initialised element traps.

int main(void) {
    int a _Checked[] = {1, 2, 3};
    volatile int i   = 3;
    return a[i];
}
