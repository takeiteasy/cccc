// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @json_skip and @json_name cannot share a member

#include <cccc/derive.h>
#include <cccc/json.h>

struct P {
    int x @json_skip @json_name("y");
};

int main(void) {
    return 42;
}
