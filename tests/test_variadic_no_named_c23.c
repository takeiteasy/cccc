// CCCC_FLAGS: --std=c23 --compiler-family=clang
// CCCC_REJECT_STDERR: named parameter

int f(...);

int main(void) {
    return 42;
}
