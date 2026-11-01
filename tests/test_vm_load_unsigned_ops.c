// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: : LDR_UB
// CCCC_EXPECT_STDOUT: : LDR_UW
// CCCC_EXPECT_STDOUT: : LDR_LOCAL_UB
// CCCC_EXPECT_STDOUT: : LDR_LOCAL_UW
// CCCC_EXPECT_STDOUT: : LDR_INDEX_UB
// CCCC_EXPECT_STDOUT: : LDR_INDEX_UW
// An unsigned char or unsigned int load is one zero-extending opcode, in its
// plain, stack-local and indexed form.
#include <stdbool.h>

unsigned      gu[2] = {0xFFFFFFFFu, 7u};
unsigned char gc[2] = {0xFF, 7};

[[gnu::noinline]] unsigned long long load_u32(const unsigned *p) {
    return *p;
}
[[gnu::noinline]] unsigned long long load_u8(const unsigned char *p) {
    return *p;
}
[[gnu::noinline]] unsigned long long index_u32(const unsigned *p, int i) {
    return p[i];
}
[[gnu::noinline]] unsigned long long index_u8(const unsigned char *p, int i) {
    return p[i];
}
[[gnu::noinline]] unsigned long long local_u32(unsigned v) {
    volatile unsigned x = v;
    return x;
}
[[gnu::noinline]] unsigned long long local_u8(unsigned char v) {
    volatile unsigned char x = v;
    return x;
}

int main(void) {
    if (load_u32(gu) != 0xFFFFFFFFull || load_u8(gc) != 0xFFull)
        return 1;
    if (index_u32(gu, 0) != 0xFFFFFFFFull || index_u8(gc, 0) != 0xFFull)
        return 2;
    if (local_u32(0xFFFFFFFFu) != 0xFFFFFFFFull || local_u8(0xFF) != 0xFFull)
        return 3;
    return 42;
}
