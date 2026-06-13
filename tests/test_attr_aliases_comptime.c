__comptime int helper(void) { return 21; }

__comptime__(inline)
$node_t *answer(void) {
    return $int_literal(helper() * 2);
}

int main(void) {
    return answer();
}
