// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __attribute__\(\(constructor\)\) void plain\(void\)
// CCCC_REJECT_STDOUT: __CCCC_CTOR|__cccc_init_dispatch
// Without any priority the constructor keeps its plain attribute.
int seen;

__attribute__((constructor)) void plain(void) {
    seen = 42;
}

int main(void) {
    return seen;
}
