// Ticket #683: a #pragma cccc comptime begin block left unclosed at the end
// of an included header is auto-closed and should emit -Wcomptime-block-leak.
// CCCC_FLAGS: -Wcomptime-block-leak
// CCCC_EXPECT_STDERR: warning:.*unclosed #pragma cccc comptime
// begin.*\[-Wcomptime-block-leak\]

#include @shared "comptime_block_leak.h"

[[cccc::comptime]]
void generate_result(void) {
    Obj *fn = MakeFunction("result", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(leak_double(21))));
}

generate_result();

int main(void) {
    return result();
}
