// Libc calls on VM memory and on host pointers libc returns stay allowed.
// CCCC_LEAKS_SKIP: comptime libc allocation still reported lost (#1728)
#include[[cccc::comptime]] < math.h>
#include[[cccc::comptime]] < stdio.h>
#include[[cccc::comptime]] < stdlib.h>
#include[[cccc::comptime]] < string.h>
#include[[cccc::comptime]] < time.h>

[[cccc::comptime]] static int cmp_int(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

[[cccc::comptime]]
Node *check(void) {
    int   bad  = 0;

    char *dup  = strdup("hello");
    bad       += dup[1] != 'e' || strlen(dup) != 5;
    free(dup);

    const char *err  = strerror(2);
    bad             += err[0] == 0;

    char buf[32];
    snprintf(buf, sizeof buf, "%s-%d-%.2f", "ab", 7, 1.5);
    bad += strcmp(buf, "ab-7-1.50") != 0;
    strcat(buf, "!");
    bad         += strlen(buf) != 10;

    int nums[4]  = {3, 1, 2, 0};
    qsort(nums, 4, sizeof nums[0], cmp_int);
    bad    += nums[0] != 0 || nums[3] != 3;

    int  n  = 0;
    char word[8];
    bad        += sscanf("12 foo", "%d %7s", &n, word) != 2 || n != 12;

    char *heap  = malloc(16);
    memset(heap, 'x', 16);
    heap  = realloc(heap, 64);
    bad  += heap[15] != 'x';
    free(heap);

    time_t     t   = 0;
    struct tm *tm  = gmtime(&t);
    bad           += tm->tm_year != 70;

    char *as        = NULL;
    bad += asprintf(&as, "%s/%d", "a", 5) != 3 || strcmp(as, "a/5") != 0;
    free(as);

    int ex;
    bad += frexp(8.0, &ex) != 0.5 || ex != 4;

    fputs("", stdout);
    fflush(stdout);
    return __builtin_ast_int_literal(bad ? 1 : 42);
}

int main(void) {
    return check();
}
