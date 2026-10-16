// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @json_skip applies to a struct member

#include <cccc/derive.h>
#include <cccc/json.h>

@json_skip int g;

int main(void) {
    return 42;
}
