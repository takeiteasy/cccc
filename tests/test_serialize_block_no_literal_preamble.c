// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: struct __cccc_block \{
//
// #990/#993: serialize_block_preamble() used to bail out entirely (no
// `struct __cccc_block` definition, no __cccc_block_copy_impl, no free()
// declaration) whenever the TU declared no block *literal* -- even when a
// block *type* was reachable through a parameter and Block_copy/
// Block_release/a block call were all used on it. struct __cccc_block is
// now also emitted when a block type is reachable at all.
//
// Not runnable end-to-end (no main / no block literal to actually invoke),
// so this is a pure -m shape assertion.

typedef int (^IntBlock)(void);

int consume(IntBlock b) {
    IntBlock c = Block_copy(b);
    int      r = c();
    Block_release(c);
    return r;
}
