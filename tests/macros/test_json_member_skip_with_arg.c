// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @json_skip takes no arguments

#include <cccc/derive.h>
#include <cccc/json.h>

struct P {
    int x @json_skip(1);
};

int main(void) {
    return 42;
}
