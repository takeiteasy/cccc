// A comma operator with a side effect on the left is not a constant
// expression, so the array is a VLA and the left side runs exactly once.
static int calls;
static int side_effect(void) {
    return ++calls;
}

int main(void) {
    int           a[(side_effect(), 3)];
    unsigned long n = sizeof(int[(side_effect(), 2)]);
    return sizeof a == 3 * sizeof(int) && n == 2 * sizeof(int) && calls == 2
               ? 42
               : 1;
}
