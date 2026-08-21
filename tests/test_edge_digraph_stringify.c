// CCCC_EXPECT_STDOUT: Hello, world!
// Tests: variadic function-like macro redefining puts; uses %: digraph
// (equivalent to #) for stringification inside the macro body.
// Godbolt: exit 0, stdout "Hello, world!"
#include <stdio.h>

#define puts(x...) puts(%:x)

int main() {
    puts(Hello, world!);
}
