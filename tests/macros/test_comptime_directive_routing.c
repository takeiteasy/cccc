#define @comptime CT_VALUE 42
#ifdef @comptime CT_VALUE
#define @comptime CT_SEEN 1
#endif @comptime

[[cccc::comptime(inline)]]
$node_t *answer(void) {
#if CT_SEEN
    return $int_literal(CT_VALUE);
#else
    return $int_literal(0);
#endif
}

int main(void) {
    return answer();
}
