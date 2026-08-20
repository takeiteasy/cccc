// CCCC_FLAGS: --testing
//
// #965: a block literal with no explicit return type (`^{ ... }` /
// `^(params){ ... }`) defaulted to TY_VOID at parse time -- harmless in the
// VM (a block's result always comes back through REG_A0/FREG_A0 regardless
// of the declared type), but wrong for the native serializer, which needs a
// real return type to spell out the lifted function's signature. Inferred
// from the body's own `return` statements now, matching clang's block
// literal inference.

[[cccc::test(return = 42)]]
int test_block_return_type_inferred_double(void) {
    double (^d)(void) = ^{
      return 1.5;
    };
    if (d() != 1.5)
        return 1;
    return 42;
}

[[cccc::test(return = 42)]]
int test_block_return_type_inferred_void(void) {
    __block int seen = 0;
    void (^v)(void)  = ^{
      seen = 1;
    };
    v();
    if (!seen)
        return 1;
    return 42;
}

[[cccc::test(return = 42)]]
int test_block_return_type_inferred_int_param(void) {
    int (^add)(int) = ^(int x) {
      return x + 21;
    };
    if (add(21) != 42)
        return 1;
    return 42;
}
