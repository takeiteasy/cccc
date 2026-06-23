// Regression test for #591: the conditional operator `cond ? a : b` must yield a
// *pointer* type (not int) when one arm is a pointer and the other is a null
// pointer constant, or when both arms are pointers.
//
// Previously cccc ran the usual arithmetic conversions on such operands, casting
// the pointer arm down to int and truncating 64-bit pointers to 32 bits. That
// corrupted any pointer returned through an idiom like
//     return n <= 0 ? 0 : malloc(n);
// which is exactly how sqlite3_malloc() is written — SQLite crashed on the first
// allocation because the returned pointer had its high 32 bits dropped.

#include <stdlib.h>
#include <string.h>

// Mirrors sqlite3_malloc: null-constant on the true arm, pointer on the false.
static void *xmalloc(int n) { return n <= 0 ? 0 : malloc((size_t)n); }

int main(void) {
    // 1. pointer obtained through `cond ? 0 : ptr` must be fully usable
    int *a = (int *)xmalloc((int)sizeof(int) * 4);
    if (!a) return 1;
    a[0] = 10; a[1] = 20; a[2] = 12;
    int sum = a[0] + a[1] + a[2];
    free(a);
    if (sum != 42) return 2;

    // 2. `cond ? ptr : 0` (pointer on the true arm) round-trips a stack address
    int x = 7;
    int *p = (1 ? &x : 0);
    if (*p != 7) return 3;
    *p = 35;
    if (x != 35) return 4;

    // 3. both arms pointers: pick the right one, no truncation
    int y = 100, z = 200;
    int *q = (0 ? &y : &z);
    if (*q != 200) return 5;

    // 4. void* / typed-pointer mix resolves to a usable pointer
    char buf[8] = "ok";
    void *vp = (1 ? (void *)buf : 0);
    if (strcmp((char *)vp, "ok") != 0) return 6;

    return 42;
}
