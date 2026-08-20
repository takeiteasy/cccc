// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __cccc_blk->__invoke\)\(__cccc_blk, 21\)
// CCCC_REJECT_STDOUT: unsupported expr kind
//
// #965: ND_BLOCK_CALL (50) had no serializer case at all -- invoking a
// block printed `/* unsupported expr kind 50 */` where the call belonged.
// Lowered to a GNU statement expression: the descriptor is evaluated once
// into a local, cast to a function pointer built from the block's own
// type, and invoked with the descriptor itself as the first ("static
// link") argument -- mirroring the VM's own invocation ABI (ND_BLOCK_CALL,
// codegen.c) exactly (descriptor in A0, user args from A1).

int main(void) {
    int (^add)(int) = ^(int x) {
      return x + 21;
    };
    return add(21);
}
