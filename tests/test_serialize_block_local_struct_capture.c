// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: struct P \{[\s\S]*struct __cccc_block_env_0
// CCCC_REJECT_STDOUT: declared inside a function
//
// #989: a by-value capture whose own struct/union type is declared inside a
// function (rather than at file scope) is now hoisted to file scope --
// ahead of the block's environment struct, which is itself emitted at file
// scope and needs the capture's type to be complete there. #965 rejected
// this with a diagnostic instead; #989 replaces that hard error with the
// hoist. Renaming only happens on a name collision (not exercised here), so
// this keeps the tag's original spelling.

int main(void) {
    struct P {
        int x;
    };
    struct P p     = {21};
    int (^b)(void) = ^{
      return p.x;
    };
    return b() + 21;
}
