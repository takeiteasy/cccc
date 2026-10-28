// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: comptime call to 'environ' denied \(class env\); pass --comptime-allow=env
// An extern bound to the host environment is gated like getenv.
extern char **environ;

[[cccc::comptime]]
int probe(void) {
    return environ && environ[0] ? 42 : 42;
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
