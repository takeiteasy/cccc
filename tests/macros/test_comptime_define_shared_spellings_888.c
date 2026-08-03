// Ticket #888: [[cccc::shared]] and __attribute__((shared)) spellings work
// identically to @shared for #define, mirroring
// test_include_shared_spellings.c's coverage of #include.
#define [[cccc::shared]] SPELL_A 21
#define __attribute__((shared)) SPELL_B 21

[[cccc::comptime]]
Node *gen(void) {
    return MakeIntLiteral(SPELL_A + SPELL_B);
}
int result = gen();

int main(void) {
    return result == 42 ? 42 : 1;
}
