#define @comptime CT_VALUE 42
#ifdef @comptime CT_VALUE
#define @comptime CT_SEEN 1
#endif @comptime

[[cccc::comptime]]
Node *answer(void) {
#if CT_SEEN
    return MakeIntLiteral(CT_VALUE);
#else
    return MakeIntLiteral(0);
#endif
}

int main(void) {
    return answer();
}
