// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: struct __cccc_block \* take_block
// CCCC_REJECT_STDOUT: unknown type
//
// #965: TY_BLOCK (17) had no serializer case -- a block-typed variable,
// parameter, return type, or struct member all printed `/* unknown type */`.
// On the default lowering path a block value is always a pointer to the
// common-initial-sequence descriptor struct (struct __cccc_block,
// serialize_block_preamble), so TY_BLOCK spells as `struct __cccc_block *`
// -- covering every declarator position (parameter, return type, struct
// member) with the one case, since serialize_type_decl's default branch
// ("<type> <name>") already handles an atomic pointer-sized type correctly
// without needing its own recursion rule.

struct holder {
    int (^callback)(int);
};

int (^take_block)(int (^b)(int)) = 0;

int main(void) {
    struct holder h;
    h.callback = 0;
    return 42;
}
