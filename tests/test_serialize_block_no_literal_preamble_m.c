// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: struct __cccc_block \{
// CCCC_EXPECT_STDOUT: __cccc_block_copy_impl
// CCCC_EXPECT_STDOUT: extern void free\(void \*\);
// CCCC_HOST_CC
// -m output defines struct __cccc_block and its helpers for a TU that uses a
// block type without a block literal.
typedef int (^IntBlock)(void);
int consume(IntBlock b) {
    IntBlock c = Block_copy(b);
    int      r = c();
    Block_release(c);
    return r;
}
