// CCCC_FLAGS: -m -G
// CCCC_EXPECT_STDOUT: #ifdef _WIN32.*int macro_body_emit\(void\);.*#endif
[[cccc::comptime]]
void gen(void) {
    EmitDirective("#ifdef _WIN32");
    Obj *fn = MakeFunction("macro_body_emit", GetType("int"));
    FunctionSetBody(fn, Quote("return 42;"));
    PublishNode(fn);
    EmitDirective("#endif");
}

gen();

int main(void) { return 42; }
