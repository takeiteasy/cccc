// CCCC_FLAGS: --comptime-allow=fs
// getline/getdelim loops at comptime reuse and grow the guest buffer.
#include[[cccc::comptime]] < stdio.h>
#include[[cccc::comptime]] < stdlib.h>
#include[[cccc::comptime]] < string.h>

[[cccc::comptime]]
int probe(void) {
    FILE *f = tmpfile();
    if (!f)
        return 1;
    fputs("a\nbb\nthis line is longer than the initial buffer\nccc;dd\n", f);
    rewind(f);

    char   *line  = NULL;
    size_t  cap   = 0;
    ssize_t n     = 0;
    int     lines = 0, total = 0;
    while ((n = getline(&line, &cap, f)) != -1) {
        lines++;
        total += (int)n;
    }
    if (lines != 4 || total != 2 + 3 + 44 + 7)
        return 2;
    rewind(f);
    n = getdelim(&line, &cap, ';', f);
    if (n != 2 + 3 + 44 + 4 || line[n - 1] != ';')
        return 3;
    if (getdelim(&line, &cap, ';', f) != 3 || strcmp(line, "dd\n") != 0)
        return 4;
    free(line);
    fclose(f);
    return 42;
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
