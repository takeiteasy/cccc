// CCCC_FLAGS: -c=generated -o /dev/stdout
// CCCC_EXPECT_STDOUT: #include <stddef.h>
// CCCC_EXPECT_STDOUT: int generated_attr_target\(void\);
// CCCC_REJECT_STDOUT: @emit
// CCCC_REJECT_STDOUT: @comptime
#include @emit <stddef.h>

@comptime
void gen(void) {
    PublishNode(MakeFunction("generated_attr_target", GetType("int")));
}

gen();

int main(void) { return 42; }
