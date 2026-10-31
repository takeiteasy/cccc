// SwitchAddCase stores a label as the condition's register holds it: an
// unsigned 32-bit and an unsigned 64-bit condition both match their label.

[[cccc::comptime]]
Node *make_wide_switches(void) {
    Type *u32 = GetType("unsigned");
    Type *u64 = GetType("unsigned long");
    Type *i32 = GetType("int");

    Obj  *f32 = MakeFunction("sw_u32", i32);
    FunctionAddParam(f32, "x", u32);
    WithFn(f32) {
        Node *sw = MakeSwitch(MakeParamRef(f32, "x"));
        SwitchAddCase(sw, MakeIntLiteral(4294967295),
                      MakeReturn(MakeIntLiteral(1)));
        SwitchAddCase(sw, MakeIntLiteral(7), MakeReturn(MakeIntLiteral(2)));
        SwitchSetDefault(sw, MakeReturn(MakeIntLiteral(0)));
        FunctionSetBody(f32, sw);
    }

    Obj *f64 = MakeFunction("sw_u64", i32);
    FunctionAddParam(f64, "x", u64);
    WithFn(f64) {
        Node *sw = MakeSwitch(MakeParamRef(f64, "x"));
        SwitchAddCase(sw, MakeIntLiteral(4294967296),
                      MakeReturn(MakeIntLiteral(1)));
        SwitchAddCase(sw, MakeIntLiteral(7), MakeReturn(MakeIntLiteral(2)));
        SwitchSetDefault(sw, MakeReturn(MakeIntLiteral(0)));
        FunctionSetBody(f64, sw);
    }
    return MakeIntLiteral(0);
}
make_wide_switches();

int sw_u32(unsigned x);
int sw_u64(unsigned long x);

int main(void) {
    if (sw_u32(0xFFFFFFFFu) != 1 || sw_u32(7) != 2 || sw_u32(8) != 0)
        return 1;
    if (sw_u64(4294967296UL) != 1 || sw_u64(7) != 2 || sw_u64(0) != 0)
        return 2;
    return 42;
}
