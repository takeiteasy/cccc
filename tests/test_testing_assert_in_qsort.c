// CCCC_FLAGS: --testing --test-timeout=5
// CCCC_EXPECT_STDERR: ^ok 2 - after
// CCCC_EXPECT_STDERR: ^not ok 1 - in_comparator
// An Assert inside a qsort comparator fails its test without breaking later
// ones.
#include <stdlib.h>

static int cmp(const void *a, const void *b) {
    Assert(0);
    return *(const int *)a - *(const int *)b;
}

[[cccc::test]]
void in_comparator(void) {
    int v[3] = {3, 1, 2};
    qsort(v, 3, sizeof v[0], cmp);
}

static int plain(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

[[cccc::test]]
void after(void) {
    int v[3] = {3, 1, 2};
    qsort(v, 3, sizeof v[0], plain);
    Assert(v[0] == 1 && v[2] == 3);
}
