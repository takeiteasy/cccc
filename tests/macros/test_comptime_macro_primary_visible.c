// #define macros in the primary source file must remain visible inside comptime
// function bodies. Guards against the filter being too aggressive.
#define PRIMARY_ANSWER 42

[[cccc::comptime]]
int get_answer(void) {
    return PRIMARY_ANSWER; // must be visible — defined in primary file
}

[[cccc::comptime]]
void generate_result(void) {
    Obj *fn = MakeFunction("result", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(get_answer())));
}

generate_result();

int main(void) {
    return result();
}
