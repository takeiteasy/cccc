// EXPECT_RUNTIME_ERROR
// GNU vector_size integer lane modulo (tracker #715): per-lane trap on a
// zero divisor, same policy as vector integer division.

typedef int v4si __attribute__((vector_size(16)));

int main(void) {
    v4si a = {1, 2, 3, 4};
    v4si b = {1, 0, 1, 1}; // lane 1 is a zero divisor
    v4si c = a % b;
    return c[0] == 0 ? 42 : 1;
}
