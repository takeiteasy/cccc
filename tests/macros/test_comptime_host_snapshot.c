// CCCC_FLAGS: --comptime-allow=fs,env
// Static buffers and caller-owned results libc returns are readable at
// comptime.
#include[[cccc::comptime]] < locale.h>
#include[[cccc::comptime]] < pwd.h>
#include[[cccc::comptime]] < stdlib.h>
#include[[cccc::comptime]] < string.h>
#include[[cccc::comptime]] < unistd.h>

[[cccc::comptime]]
int probe(void) {
    int         bad    = 0;

    const char *path   = getenv("PATH");
    bad               += !path || strlen(path) == 0;

    struct passwd *pw  = getpwnam("root");
    bad               += !pw || pw->pw_uid != 0 || strcmp(pw->pw_name, "root");

    struct lconv *lc   = localeconv();
    bad               += lc->decimal_point[0] != '.';

    char *cwd          = getcwd(NULL, 0);
    bad               += !cwd || cwd[0] != '/';
    free(cwd);

    char *real  = realpath("/dev/null", NULL);
    bad        += !real || strcmp(real, "/dev/null") != 0;
    free(real);

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
