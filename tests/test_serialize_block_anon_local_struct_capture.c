// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: struct __cccc_local_anon_0 \{[\s\S]*struct
// __cccc_block_env_0 CCCC_REJECT_STDOUT: unsupported expr kind
//
// #989: a *tagless* function-local aggregate has no TypeName record at all,
// so type_decl_owner() alone can't distinguish it from an ordinary
// file-scope type -- without special handling it would keep falling through
// to serialize_anon_aggregate(), inlining a fresh anonymous struct body at
// every use site instead of one shared, hoisted definition. This asserts a
// synthesized `__cccc_local_anon_` tag is used instead of an inline body.

int main(void) {
    struct {
        int x;
    } p            = {21};
    int (^b)(void) = ^{
      return p.x;
    };
    return b() + 21;
}
