// CCCC_FLAGS: -3
// Legal accesses over fixed-size arrays are not rejected by the static-size
// bounds check: last element, one-past-the-end pointer formation, row
// decay, a struct's trailing array idiom, and global arrays.
struct Flex {
    int  n;
    char data[1];
};

static int        garr[16];
static const char gstr[] = "hello";

static int sum_range(const int *p, const int *end) {
    int s = 0;
    for (; p < end; p++)
        s += *p;
    return s;
}

int main(int argc, char **argv) {
    (void)argv;
    int  a[8];
    int  m[4][4];
    char buf[16];
    for (int i = 0; i < 8; i++)
        a[i] = i;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            m[i][j] = i * 4 + j;
    for (int i = 0; i < 16; i++)
        buf[i] = (char)i;
    for (int i = 0; i < 16; i++)
        garr[i] = 1;

    int k  = argc > 100 ? 0 : 7; // last element, via a runtime index
    a[k]  += 1;
    a[k]++;
    m[3][3]         = a[k];

    const int *end  = &a[8]; // one past the end: formed, never dereferenced
    int        s    = sum_range(a, end) + sum_range(a, a + 8);
    s              += (int)(&buf[16] - &buf[0]);
    s              += m[3][3] + garr[15] + (int)gstr[4];

    struct Flex f;
    char       *d = f.data;
    d[0]          = 1;
    f.n           = 1;

    // a[7] was bumped twice, so each sum is 0+1+...+6 + 9
    int expect = (0 + 1 + 2 + 3 + 4 + 5 + 6 + 9) * 2 + 16 + 9 + 1 + 'o';
    return s == expect ? 42 : 1;
}
