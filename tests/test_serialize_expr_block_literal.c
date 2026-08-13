// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: struct __cccc_block_env_0 \{[\s\S]*__cccc_tmp0.__cap0 = a
// CCCC_REJECT_STDOUT: unsupported expr kind
//
// #965: ND_BLOCK_LITERAL (49) had no serializer case at all -- `^{ ... }`
// printed `/* unsupported expr kind 49 */` in its place. The default
// lowering (lift + explicit env struct, resolved on the ticket) emits one
// `struct __cccc_block_env_N` per block literal (its captures, in
// descriptor-slot order) and builds the literal itself as a comma
// expression writing into its descriptor local -- mirroring the VM's own
// stack-descriptor construction (ND_BLOCK_LITERAL, codegen.c) exactly.

int main(void) {
    int a = 21;
    int (^add)(int) = ^(int x) { return x + a; };
    return add(21);
}
