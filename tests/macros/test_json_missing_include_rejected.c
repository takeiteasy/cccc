// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @from_json needs #include <cccc/json.h>

#include <cccc/derive.h>

@from_json
struct Point {
    int x;
};

int main(void) {
    return 42;
}
