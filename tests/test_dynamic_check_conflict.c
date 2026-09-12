// #486: `_Dynamic_check` is recognized as the builtin alias ONLY when no
// symbol of that name is already in scope (primary(), src/parse_postfix.c,
// via a find_var() lookup) -- a real user function named `_Dynamic_check`
// is called normally, the builtin spelling never shadows it.

int _Dynamic_check(int x) {
    return x + 1;
}

int main(void) {
    return _Dynamic_check(41);
}
