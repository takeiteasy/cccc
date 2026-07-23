// EXPECT_RUNTIME_ERROR
// GNU vector_size integer lane division (tracker #715): per-lane trap on a
// zero divisor, mirroring scalar DIVC's trapping policy (not DIV3's
// non-trapping LLONG_MIN return) -- vector integer division is deliberately
// stricter, per the div-by-zero policy decided for this ticket.

typedef int v4si __attribute__((vector_size(16)));

int main(void) {
    v4si a = {1, 2, 3, 4};
    v4si b = {1, 0, 1, 1}; // lane 1 is a zero divisor
    v4si c = a / b;
    return c[0] == 1 ? 42 : 1;
}
