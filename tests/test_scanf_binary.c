// Test: C23 %b/%B (binary integer) conversion specifier - scanf family
// Returns: 42

#include "stdio.h"

int main() {
    int a, b, c, d;

    // Plain binary digits, no prefix
    sscanf("101010", "%b", &a);
    printf("a=%d\n", a);  // 42

    // 0b / 0B prefix is accepted
    sscanf("0b101010", "%b", &b);
    printf("b=%d\n", b);  // 42

    sscanf("0B1111", "%B", &c);
    printf("c=%d\n", c);  // 15

    // Field width limits how many chars are consumed
    sscanf("101111", "%4b", &d);
    printf("d=%d\n", d);  // 1011 -> 11

    // Multiple conversions in one format string
    int x, y, z;
    int n = sscanf("10 0x1F 0b110", "%d %x %b", &x, &y, &z);
    printf("n=%d x=%d y=%d z=%d\n", n, x, y, z);  // n=3 x=10 y=31 z=6

    // %i auto-detects binary via 0b prefix
    int w;
    sscanf("0b1010", "%i", &w);
    printf("w=%d\n", w);  // 10

    // Matching failure: a leading non-binary digit means no conversion
    int e = -1;
    int n2 = sscanf("29", "%b", &e);
    printf("n2=%d e=%d\n", n2, e);  // n2=0, e unchanged (-1)

    // File-based round trip with %#b
    FILE *f = fopen("/tmp/test_scanf_binary.txt", "w+");
    fprintf(f, "%#b %d", 13u, 99);
    fflush(f);
    rewind(f);
    int fa, fb;
    int fn = fscanf(f, "%b %d", &fa, &fb);
    printf("fn=%d fa=%d fb=%d\n", fn, fa, fb);  // fn=2 fa=13 fb=99
    fclose(f);
    remove("/tmp/test_scanf_binary.txt");

    return 42;
}
