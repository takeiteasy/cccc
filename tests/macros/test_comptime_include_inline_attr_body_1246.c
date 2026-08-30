// Test #1246: a routed comptime include followed by a [[cccc::comptime]]
// function whose attribute+signature share a line but whose body starts on
// the next line, alongside a sibling comptime function that #defines a
// macro. Previously the missing line boundary after the injected #include
// made skip_line() land on the body's opening brace and jump over the
// TK_MACRO_SCOPE_PUSH marker meant to isolate the sibling's #define,
// desyncing the macro scope stack ("internal error: unbalanced compile-time
// macro scope stack").

#include @comptime <glob.h>

[[cccc::comptime]] int glob_type_size(void)
{
    glob_t g;
    return sizeof(g) > 0;
}

[[cccc::comptime]]
void generate_result(void) {
#define LOCAL_ANSWER 42
    Obj *fn = MakeFunction("result", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(
                             glob_type_size() ? LOCAL_ANSWER : 1)));
#undef LOCAL_ANSWER
}

generate_result();

int main(void) {
    return result();
}
