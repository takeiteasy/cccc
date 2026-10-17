// Comptime reaches string, snprintf and malloc without --comptime-allow.
#include[[cccc::comptime]] <stdio.h>
#include[[cccc::comptime]] <stdlib.h>
#include[[cccc::comptime]] <string.h>

[[cccc::comptime]]
int probe(void) {
    char buf[16];
    snprintf(buf, sizeof buf, "%d", 41);
    char *p = malloc(4);
    strcpy(p, "ab");
    int n = (int)strlen(buf) + (int)strlen(p);
    free(p);
    return n + 38;
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
