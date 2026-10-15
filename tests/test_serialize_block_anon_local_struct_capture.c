// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: struct __cccc_anon0 \{[\s\S]*struct __cccc_block_env_0
// CCCC_REJECT_STDOUT: unsupported expr kind
//
// #989: a *tagless* function-local aggregate has no TypeName record of its
// own, so without special handling it would be spelled as a fresh inline
// struct body at every use site instead of one shared definition. This
// asserts a synthesized `__cccc_anon<N>` tag is used instead of an inline
// body.

int main(void) {
    struct {
        int x;
    } p            = {21};
    int (^b)(void) = ^{
      return p.x;
    };
    return b() + 21;
}
