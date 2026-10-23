// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __atomic_add_fetch
// CCCC_EXPECT_STDOUT: __atomic_sub_fetch
// CCCC_EXPECT_STDOUT: __atomic_and_fetch
// CCCC_EXPECT_STDOUT: __atomic_or_fetch
// CCCC_EXPECT_STDOUT: __atomic_xor_fetch
// CCCC_EXPECT_STDOUT: __atomic_fetch_add
// CCCC_REJECT_STDOUT: __atomic_compare_exchange_n
// CCCC_HOST_CC: -std=gnu11 -Wunused-value -Werror -Wno-unused-variable
// -m output lowers _Atomic compound assignment and ++/-- on integers and
// pointers to one fetch-and-op builtin, with no unused-value warning.
struct S {
    _Atomic int c;
};
int main(void) {
    struct S    s = {0};
    _Atomic int x = 5;
    int        *arr[1];
    int *_Atomic p  = (int *)arr;
    s.c            += 2;
    x              -= 1;
    x              &= 6;
    x              |= 8;
    x              ^= 1;
    p              += 2;
    x++;
    ++x;
    int old = x++;
    int nw  = ++x;
    return s.c == 2 && x == 17 && old == 15 && nw == 17 && p != 0 ? 42 : 1;
}
