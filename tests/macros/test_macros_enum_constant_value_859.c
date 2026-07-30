// EnumConstantValue()/__builtin_ast_enum_constant_value must round-trip the
// full int64_t range of a C23 wide-underlying-type enum constant. The FFI
// registration for this builtin used to be hand-typed as `int` in
// src/macros.c's forward-declaration block (while reflection.h and
// reflection.c both correctly said int64_t) -- a conflicting-declaration
// signature drift that would truncate any constant beyond INT32_MAX.
// src/reflection_ffi_protos.inc (generated from reflection.h by
// tools/gen_reflection_ffi.py) is now #include'd by both src/macros.c and
// src/reflection.c, so that class of drift is a compile error instead.

enum BigVal : long long {
    BV_BIG = 0x300000000LL, // > INT32_MAX, would truncate/wrap under `int`
    BV_NEG = -0x300000000LL,
};

[[cccc::comptime]]
Node *check_enum_constant_value(void) {
    Type *ty = GetType("BigVal");

    EnumConstant *big = EnumFind(ty, "BV_BIG");
    if (!big || EnumConstantValue(big) != 0x300000000LL)
        return MakeIntLiteral(1);

    EnumConstant *neg = EnumFind(ty, "BV_NEG");
    if (!neg || EnumConstantValue(neg) != -0x300000000LL)
        return MakeIntLiteral(2);

    return MakeIntLiteral(42);
}

int main(void) {
    return check_enum_constant_value();
}
