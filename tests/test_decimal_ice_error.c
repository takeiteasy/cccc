// EXPECT_COMPILE_ERROR
// C23: a _Decimal value whose *own* type is decimal cannot appear directly
// in an integer constant expression (array bound, case label, bit-field
// width, _Static_assert, enumerator) -- must produce a clean diagnostic,
// never a silent 0. This is still rejected in both build configurations
// after #832: `(int)1.5dd` (a decimal-to-integer *cast*, the immediate
// operand case ISO C actually permits in an ICE) now folds correctly --
// see tests/test_decimal_ice_fold.c -- but a bare, uncast decimal literal
// used directly as an integer constant expression is not something #832
// added folding for (there is no destination width to fold *to* without a
// cast), so it remains a diagnostic.

int arr[1.5dd];

int main(void) {
    return sizeof(arr);
}
