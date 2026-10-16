// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: duplicate JSON key 'b'

#include <cccc/derive.h>
#include <cccc/json.h>

@to_json struct P {
    int a @json_name("b");
    int b;
};

int main(void) {
    return 42;
}
