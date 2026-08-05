// CCCC_FLAGS: --comptime-include-all
// Non-system headers already resolve on demand without this flag (#894);
// --comptime-include-all's remaining job is widening the demand-driven
// declaration index to *system* headers too (glob_t here), plus forwarding
// all #define macros. Compare test_comptime_strict_blocks_regular_include.c,
// which pins the same header as unavailable WITHOUT the flag.
#include <glob.h>

[[cccc::comptime]]
int glob_struct_nonempty(void) {
    return (int)sizeof(glob_t) > 0;
}

[[cccc::comptime]]
void generate_result(void) {
    Obj *fn = MakeFunction("result", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(glob_struct_nonempty() ? 42 : 1)));
}

generate_result();

int main(void) {
    return result();
}
