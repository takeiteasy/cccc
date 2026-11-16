// A variable-length array declared in a statement expression stays in scope
// for the statements after it.
int main(void) {
    unsigned long n = 40;
    int           r = ({
        char b[n];
        b[0] = 2;
        (int)sizeof(b) + b[0];
    });
    return r;
}
