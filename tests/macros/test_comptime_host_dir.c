// CCCC_FLAGS: --comptime-allow=fs
// Directory and glob results libc keeps owning are readable until released.
#include[[cccc::comptime]] < dirent.h>
#include[[cccc::comptime]] < glob.h>
#include[[cccc::comptime]] < stdlib.h>
#include[[cccc::comptime]] < string.h>

[[cccc::comptime]]
static int only_null(const struct dirent *e) {
    return strcmp(e->d_name, "null") == 0;
}

[[cccc::comptime]]
int probe(void) {
    int    bad = 0;

    glob_t g;
    bad += glob("/dev/nul*", 0, NULL, &g) != 0;
    bad += g.gl_pathc != 1 || strcmp(g.gl_pathv[0], "/dev/null") != 0;
    glob("/dev/zer*", GLOB_APPEND, NULL, &g);
    bad += g.gl_pathc != 2 || strcmp(g.gl_pathv[1], "/dev/zero") != 0;
    bad += strcmp(g.gl_pathv[0], "/dev/null") != 0;
    globfree(&g);

    struct dirent **list;
    int             n  = scandir("/dev", &list, only_null, alphasort);
    bad               += n != 1 || strcmp(list[0]->d_name, "null") != 0;
    for (int i = 0; i < n; i++)
        free(list[i]);
    free(list);

    DIR *d    = opendir("/dev");
    bad      += !d;
    int seen  = 0;
    for (struct dirent *e; d && (e = readdir(d));)
        seen += strcmp(e->d_name, "null") == 0;
    bad += seen != 1;
    if (d)
        closedir(d);

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
