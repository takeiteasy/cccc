// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @to_json expects a struct/union target

#include <cccc/derive.h>
#include <cccc/json.h>

@to_json
enum Color { RED, GREEN };

int main(void) {
    return 42;
}
