// CCCC_FLAGS: -V
// Comptime allocations stay usable when -V turns the VM heap off for the program.
#include[[cccc::comptime]] < stdlib.h>
#include[[cccc::comptime]] < string.h>

[[cccc::comptime]]
int probe(void) {
    char *k = malloc(2);
    strcpy(k, "k");
    int bad = k[0] != 'k';
    free(k);

    int *z = calloc(4, sizeof(int));
    bad   += z[3] != 0;
    z      = realloc(z, 8 * sizeof(int));
    z[7]   = 5;
    bad   += z[7] != 5;
    free(z);

    void *(*alloc)(size_t) = malloc;
    char *p                = alloc(4);
    strcpy(p, "abc");
    bad += strlen(p) != 3;
    free(p);
    return bad;
}

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("result", GetType("int"));
    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(probe() == 0 ? 42 : 1)));
}

gen();

int main(void) {
    return result();
}
