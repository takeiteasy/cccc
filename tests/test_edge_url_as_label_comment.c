// Tests: a URL literal in function body parses as a valid C statement —
// "https:" is a goto label, "//git.sr.ht/..." is a C99 line comment.
int main() {
https://git.sr.ht/~takeiteasy/cccc
    return 42;
}
