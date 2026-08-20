// EXPECT_COMPILE_ERROR
// A bounds form (count/byte_count/bounds) only makes sense paired with a
// checked kind (array/ntarray) -- see pointers()'s cross-check in
// src/parse.c, right after the checked-pointer attribute dispatchers run.

int main(void) {
    int n                     = 3;
    int *[[cccc::count(n)]] a = (int[3]){1, 2, 3};
    return a[0];
}
