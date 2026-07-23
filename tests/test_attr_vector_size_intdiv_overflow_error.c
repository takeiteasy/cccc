// EXPECT_RUNTIME_ERROR
// GNU vector_size integer lane division (tracker #715): per-lane trap on
// INT_MIN / -1 overflow, mirroring scalar DIVC's overflow trap.

typedef int v4si __attribute__((vector_size(16)));

int main(void) {
    v4si a = {1, -2147483647 - 1, 3, 4}; // lane 1 is INT_MIN
    v4si b = {1, -1, 1, 1};
    v4si c = a / b;
    return c[0] == 1 ? 42 : 1;
}
