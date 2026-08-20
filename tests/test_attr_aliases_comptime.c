__comptime int helper(void) {
    return 21;
}

__comptime__ Node *answer(void) {
    return MakeIntLiteral(helper() * 2);
}

int main(void) {
    return answer();
}
