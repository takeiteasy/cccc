// Ticket V010 (#884): a forward-declared [[cccc::comptime]] function crashed
// cccc itself (SIGSEGV, no diagnostic) instead of compiling or erroring.
// A bodyless comptime declaration is a no-op: compile_all_macros already
// emits prototypes for every captured comptime function before any
// definition, so mutual recursion between comptime functions works without
// a forward declaration -- but the ticket's exact repro (which needed one
// for a mutual-recursion helper pair) must still compile cleanly rather
// than crash.
//
// Expected return: 42

[[cccc::comptime]]
int is_odd(int n);

[[cccc::comptime]]
int is_even(int n)
{
    if (n == 0) return 1;
    return is_odd(n - 1);
}

[[cccc::comptime]]
int is_odd(int n)
{
    if (n == 0) return 0;
    return is_even(n - 1);
}

[[cccc::comptime]]
Node *gen(void) {
    return MakeIntLiteral(is_even(10)); // 10 is even -> 1 (true)
}

int result = gen();
int main(void) { return result == 1 ? 42 : 1; }
