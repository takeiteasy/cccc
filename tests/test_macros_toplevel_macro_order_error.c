// Test ticket #229: global macro calls run pre-parse, so generated
// functions are visible everywhere regardless of source order.

[[cccc::comptime]]
void generate_late(void) {
    Type *int_ty = GetType("int");
    Obj *fn = MakeFunction("late_generated", int_ty);
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(42)));
}

int main(void) {
    return late_generated();
}

generate_late();
