@define CT_VALUE 42
@ifdef CT_VALUE
@define CT_SEEN 1
@endif

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
