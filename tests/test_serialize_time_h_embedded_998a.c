// CCCC_FLAGS: -c=generated -o /dev/stdout
// CCCC_EXPECT_STDOUT: #include <time\.h>
// CCCC_EXPECT_STDOUT: use_it
// CCCC_REJECT_STDOUT: struct tm \{
// CCCC_HOST_CC
// CCCC_NO_INCLUDE_DIR
// -c=generated for an embedded standard header both @comptime-routed and
// plainly included has no duplicate struct tm.
#include @comptime < time.h>
#include <time.h>
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
