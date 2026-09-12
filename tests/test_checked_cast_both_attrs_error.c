// EXPECT_COMPILE_ERROR
// #486: 'assume' and 'dynamic' are mutually exclusive -- caught in
// apply_checked_ptr_attr() (src/parse_checked.c) the moment the second one
// is seen, regardless of attribute order within the bracket list.

int main(void) {
    int arr[5] = {1, 2, 3, 4, 5};
    int n      = 5;
    int *[[cccc::array, cccc::count(n)]] p =
        (int *[[cccc::array, cccc::count(n), cccc::assume, cccc::dynamic]])arr;
    return p[0];
}
