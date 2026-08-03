// Ticket #888: #define @shared NAME opts a single object-like macro into
// visibility during the isolated comptime preprocessing pass, without
// needing to move it into a separate @shared-included header. This is the
// ticket's exact repro shape: a #define used as a local array size inside a
// [[cccc::comptime]] function body.
#define @shared AOT_MAX_SCOPE 64

[[cccc::comptime]]
Node *gen(void) {
    int floats[AOT_MAX_SCOPE];
    return MakeIntLiteral((int)(sizeof(floats) / sizeof(int)));
}
int result = gen();

int main(void) {
    return result == 64 ? 42 : 1;
}
