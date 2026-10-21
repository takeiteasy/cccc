// -c=native: assigning one tagless-union global to another
static union {
    int  i;
    char c[4];
} u1, u2;
int main(void) {
    u1.i = 42;
    u2   = u1;
    return u2.i;
}
