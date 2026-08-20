// EXPECT_COMPILE_ERROR
// Ticket #627 supersedes #283: primary-file #define macros are NOT visible
// inside comptime function bodies. The comptime pass starts with an isolated
// macro state (CCCC builtins + -D defines only). Users must put #defines that
// are needed in comptime code into an @shared header.
#define FROM_MAIN 77

[[cccc::comptime]] int get_val(void) {
    return FROM_MAIN;
}

[[cccc::comptime]]
Node *call_get(void) {
    return MakeIntLiteral(get_val());
}

int main(void) {
    return call_get() == 77 ? 42 : 1;
}
