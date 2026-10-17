// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @to_json: owned member 'Outer.in' points to a struct that is not declared yet; declare it first

#include <cccc/derive.h>
#include <cccc/json.h>

struct Later;

@to_json struct Outer {
    struct Later *in @owned;
};

struct Later {
    int v;
};

int main(void) {
    return 42;
}
