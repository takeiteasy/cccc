// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @compare: member 'Node.next' has an unsupported type

#include <cccc/derive.h>

@compare struct Node {
    int          v;
    struct Node *next;
};

int main(void) {
    return 42;
}
