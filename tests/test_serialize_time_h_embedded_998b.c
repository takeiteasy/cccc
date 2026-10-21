// CCCC_FLAGS: -c=generated -o /dev/stdout
// CCCC_EXPECT_STDOUT: struct tm \{
// CCCC_REJECT_STDOUT: #include <time\.h>
// CCCC_HOST_CC
// CCCC_NO_INCLUDE_DIR
// -c=generated for an embedded standard header reached only through #include
// @comptime still derives its definition.
#include @comptime < time.h>
[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("use_it", GetType("int"));
    FunctionSetBody(
        fn, Quote("{ struct tm t; t.tm_year = 42; return t.tm_year; }"));
    PublishNode(fn);
}
gen();
int use_it(void);
int main(void) {
    return use_it();
}
