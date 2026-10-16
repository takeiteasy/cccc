// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @json_name expects a non-empty string literal key

#include <cccc/derive.h>
#include <cccc/json.h>

struct P {
    int x @json_name("");
};

int main(void) {
    return 42;
}
