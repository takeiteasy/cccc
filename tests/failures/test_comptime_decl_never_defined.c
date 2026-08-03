// EXPECT_COMPILE_ERROR
// Ticket V010 (#884): a bodyless [[cccc::comptime]] declaration is a no-op
// (forward declarations are unnecessary), but if the name is never captured
// with an attributed definition, that should be a clear diagnostic rather
// than silently compiling nothing.

[[cccc::comptime]]
int f(int n);

int main(void) { return 42; }
