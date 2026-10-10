// CCCC_FLAGS: --std=c99
//
// __alignof__ and __alignof are available before C11, unlike _Alignof.
int main(void) {
    if (__alignof__(int) != 4 || __alignof(long) != 8)
        return 1;
    return 42;
}
