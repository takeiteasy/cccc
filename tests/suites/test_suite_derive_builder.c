// CCCC_FLAGS: --testing
// Suite: @builder, run on the VM and under -c=native.

#include <cccc/derive.h>
#include <string.h>

enum { DEFAULT_PORT = 8080 };

typedef enum { LOW, HIGH } Level;

@builder struct Config {
    char  host[16] @builder_required;
    int   port @builder_default(DEFAULT_PORT);
    int   retries @builder_default(1 + 2);
    _Bool tls;
    Level level @builder_default(HIGH);
    int   id @builder_required;
    struct {
        int x;
        int y @builder_default(-1);
    };
};

@builder("app") typedef struct {
    int a;
    int b @builder_default(7);
} Pair;

@builder struct Empty {
    int v;
};

[[cccc::test]] void test_defaults_and_zero_init(void) {
    Config_builder b = Config_builder_new();
    AssertEq(b.value.port, 8080);
    AssertEq(b.value.retries, 3);
    AssertEq(b.value.tls, 0);
    AssertEq((int)b.value.level, (int)HIGH);
    AssertEq(b.value.x, 0);
    AssertEq(b.value.y, -1);
    AssertEq(b.value.host[0], 0);
}

[[cccc::test]] void test_missing_required_fails_and_leaves_out(void) {
    Config_builder b = Config_builder_new();
    struct Config  c;
    memset(&c, 0x5a, sizeof c);
    AssertEq(Config_build(&b, &c) != 0, 1);
    Config_builder_host(&b, "example.org");
    AssertEq(Config_build(&b, &c) != 0, 1);
    AssertEq((int)(unsigned char)((char *)&c)[0], 0x5a);
    Config_builder_id(&b, 9);
    AssertEq(Config_build(&b, &c), 0);
    AssertEq(strcmp(c.host, "example.org"), 0);
    AssertEq(c.id, 9);
}

[[cccc::test]] void test_chaining_overrides_defaults(void) {
    Config_builder b = Config_builder_new();
    struct Config  c;
    Config_builder_id(Config_builder_host(&b, "h"), 1);
    Config_builder_tls(Config_builder_port(Config_builder_y(&b, 4), 99), 1);
    AssertEq(Config_build(&b, &c), 0);
    AssertEq(c.port, 99);
    AssertEq(c.tls, 1);
    AssertEq(c.y, 4);
    AssertEq(c.retries, 3);
    AssertEq(c.x, 0);
}

[[cccc::test]] void test_setter_returns_the_builder(void) {
    Config_builder b = Config_builder_new();
    AssertEq(Config_builder_port(&b, 1) == &b, 1);
}

[[cccc::test]] void test_builders_are_independent_values(void) {
    Config_builder a = Config_builder_new();
    Config_builder b = a;
    Config_builder_port(&b, 5);
    AssertEq(a.value.port, 8080);
    AssertEq(b.value.port, 5);
}

[[cccc::test]] void test_prefix_and_typedef_target(void) {
    app_Pair_builder b = app_Pair_builder_new();
    Pair             p;
    app_Pair_builder_a(&b, 3);
    AssertEq(app_Pair_build(&b, &p), 0);
    AssertEq(p.a, 3);
    AssertEq(p.b, 7);
}

[[cccc::test]] void test_no_required_members(void) {
    Empty_builder b = Empty_builder_new();
    struct Empty  e;
    AssertEq(Empty_build(&b, &e), 0);
    AssertEq(e.v, 0);
}
