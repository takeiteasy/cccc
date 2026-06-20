// Ticket #292: variable declarations inside a comptime block are implicitly
// [[cccc::comptime]] without requiring the attribute.

#pragma cccc comptime begin
int answer = 42;
#pragma cccc comptime end

[[cccc::comptime(inline)]]
Node *get_answer(void) {
    return GetComptimeVar("answer");
}

int main(void) {
    if (get_answer() != 42)
        return 1;
    return 42;
}
