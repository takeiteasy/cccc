// CCCC_FLAGS: --compiler-family=clang
// Under the clang family a cast keeps the typedef's aligned(N).
typedef int i64 __attribute__((aligned(64)));
typedef int plain_t;

int main(void) {
    int x = 0;
    if (_Alignof((i64)x) != 64)
        return 1;
    if (_Alignof((plain_t)x) != 4)
        return 2;
    if (_Alignof(x + (i64)x) != 4) // no right-operand bias under clang
        return 3;
    return 42;
}
