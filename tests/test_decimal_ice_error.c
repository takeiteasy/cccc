// EXPECT_COMPILE_ERROR
// C23 (#402): decimal constant folding is not implemented in phase 1 (see
// the follow-up ticket), so a _Decimal value cannot appear in an integer
// constant expression (array bound, case label, bit-field width,
// _Static_assert, enumerator) -- not even through a cast. Must produce a
// clean diagnostic, never a silent 0.

int arr[(int)1.5dd];

int main(void) {
    return sizeof(arr);
}
