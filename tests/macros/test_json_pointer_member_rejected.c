// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @to_json: member 'Outer.inner.name' has a type JSON cannot represent

#include <cccc/derive.h>
#include <cccc/json.h>

@to_json
struct Outer {
    int id;
    struct {
        const char *name;
    } inner;
};

int main(void) {
    return 42;
}
