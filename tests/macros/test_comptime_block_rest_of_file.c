// Bare #pragma cccc comptime (no 'begin') marks everything from the pragma to
// EOF as comptime; it runs to the end of the file without a closing 'end'.

#pragma cccc comptime begin

int triple(int n) {
    return n * 3;
}

int base = 14;

[[cccc::comptime(inline)]]
Node *make_answer(void) {
    return MakeIntLiteral(triple(GetComptimeInt("base")));
}

#pragma cccc comptime end

int main(void) {
    if (make_answer() != 42)
        return 1;
    return 42;
}
