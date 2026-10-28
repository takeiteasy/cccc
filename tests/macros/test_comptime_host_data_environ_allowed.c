// CCCC_FLAGS: --comptime-allow=env
// With the env grant, comptime reads a snapshot of the host environment.
extern char **environ;

[[cccc::comptime]]
int probe(void) {
    if (!environ || !environ[0])
        return 1;
    int n = 0;
    while (environ[n])
        n++;
    return n > 0 && environ[0][0] ? 42 : 2;
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
