// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #945 hoists a non-trivial member-access object expression (here, arr[k]'s
// runtime index `k`) into a single compiler-generated temp shared by lo and
// hi. This proves the out-of-bounds access through a runtime-indexed
// array-of-structs member traps.

struct S {
    int n;
    int *[[cccc::array, cccc::count(n)]] p;
};

int main(void) {
    struct S arr[2] = {
        {4, (int[4]){1, 2, 3, 4}},
        {4, (int[4]){5, 6, 7, 8}},
    };
    volatile int k = 1;
    volatile int i = 4;
    int          x = arr[k].p[i];
    return x;
}
