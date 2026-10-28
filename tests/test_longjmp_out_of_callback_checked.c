// Same as test_longjmp_out_of_callback.c under the dangling-pointer and
// epoch checks (-3): a longjmp out of a callback retires the abandoned frames.
// CCCC_FLAGS: -3
// CCCC_REJECT_STDOUT: after
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

static jmp_buf env;
static jmp_buf inner_env;
static int     calls;

static int jump_out(const void *a, const void *b) {
    if (++calls == 2)
        longjmp(env, 7);
    return *(const int *)a - *(const int *)b;
}

static int compare(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

static int jump_within(const void *a, const void *b) {
    if (setjmp(inner_env) == 0)
        longjmp(inner_env, 3);
    return *(const int *)a - *(const int *)b;
}

// bsearch inside a qsort comparator jumps to the qsort comparator's frame.
static int nested_calls;
static int nested_cmp(const void *a, const void *b);

static int bsearch_jumps(const void *a, const void *b) {
    longjmp(inner_env, 5);
}

static int nested_cmp(const void *a, const void *b) {
    int key = 1, table[] = {1, 2, 3};
    nested_calls++;
    if (setjmp(inner_env) == 0)
        bsearch(&key, table, 3, sizeof(int), bsearch_jumps);
    return *(const int *)a - *(const int *)b;
}

int main(void) {
    int a[] = {3, 1, 2, 5, 4};

    int r = setjmp(env);
    if (r) {
        if (r != 7)
            return 1;
    } else {
        qsort(a, 5, sizeof *a, jump_out);
        puts("after qsort");
        return 2;
    }

    // The sort works again afterwards.
    qsort(a, 5, sizeof *a, compare);
    for (int i = 0; i < 5; i++)
        if (a[i] != i + 1)
            return 3;

    // Repeated escapes leave no state behind.
    int escapes = 0;
    for (int i = 0; i < 1000; i++) {
        calls = 0;
        int b[] = {9, 8, 7, 6};
        if (setjmp(env) == 0)
            qsort(b, 4, sizeof *b, jump_out);
        else
            escapes++;
    }
    if (escapes != 1000)
        return 4;

    int c[] = {4, 2, 3, 1};
    qsort(c, 4, sizeof *c, jump_within);
    qsort(c, 4, sizeof *c, nested_cmp);
    if (nested_calls == 0)
        return 5;
    for (int i = 0; i < 4; i++)
        if (c[i] != i + 1)
            return 6;
    return 42;
}
