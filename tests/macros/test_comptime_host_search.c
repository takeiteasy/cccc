// Search-table entries and tree nodes libc owns are readable at comptime.
#include[[cccc::comptime]] < search.h>
#include[[cccc::comptime]] < stdlib.h>
#include[[cccc::comptime]] < string.h>

[[cccc::comptime]]
static int cmp_int(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

[[cccc::comptime]] static int g_sum;

[[cccc::comptime]]
static void visit(const void *node, VISIT which, int depth) {
    (void)depth;
    if (which == postorder || which == leaf)
        g_sum += **(int *const *)node;
}

[[cccc::comptime]]
int probe(void) {
    int bad = 0;

    hcreate(8);
    char *key = malloc(2);
    strcpy(key, "k");
    ENTRY it = {key, (void *)7};
    hsearch(it, ENTER);
    ENTRY *found        = hsearch(it, FIND);
    bad                += !found || (long)found->data != 7;
    found->data         = (void *)9;
    bad                += (long)hsearch(it, FIND)->data != 9;
    hdestroy();

    static int keys[3]  = {2, 1, 3};
    void      *root     = NULL;
    for (int i = 0; i < 3; i++)
        tsearch(&keys[i], &root, cmp_int);
    void **hit  = tfind(&keys[0], &root, cmp_int);
    bad        += !hit || *(int *)*hit != 2;
    twalk(root, visit);
    bad += g_sum != 6;

    return bad ? 1 : 42;
}

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("result", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(probe())));
}

gen();

int main(void) {
    return result();
}
