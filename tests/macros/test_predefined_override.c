// CCCC_FLAGS: -U__BYTE_ORDER__ -D__INT_MAX__=7
// A command-line -U/-D overrides the target predefines.
#ifdef __BYTE_ORDER__
#error "-U__BYTE_ORDER__ ignored"
#endif

int main(void) {
    return __INT_MAX__ == 7 ? 42 : 1;
}
