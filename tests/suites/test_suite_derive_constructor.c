// CCCC_FLAGS: --testing
// Suite: @ctor with @default and @ctor_skip, run on the VM and under
// -c=native.

#include <cccc/derive.h>
#include <string.h>

enum { DEFAULT_PORT = 8080 };

typedef enum { LOW, HIGH } Level;

@ctor struct Config {
    char *name;
    int   port @ default(DEFAULT_PORT);
    int   retries @ default(1 + 2);
    Level level @ default(HIGH);
    char *label @ default("untitled");
    int   hits @ctor_skip;
    struct {
        int x;
        int y @ default(-1);
    };
};

@ctor struct Window {
    int w @ default(80);
    int h @ default(24);
};

@ctor typedef struct {
    int  id;
    char tag[4];
} Item;

[[cccc::test]] void test_positional_and_defaults(void) {
    struct Config c = Config_new("srv", 5);
    AssertEq(strcmp(c.name, "srv"), 0);
    AssertEq(c.x, 5);
    AssertEq(c.port, 8080);
    AssertEq(c.retries, 3);
    AssertEq((int)c.level, (int)HIGH);
    AssertEq(strcmp(c.label, "untitled"), 0);
    AssertEq(c.y, -1);
}

[[cccc::test]] void test_skipped_member_is_zero(void) {
    struct Config c = Config_new("srv", 5);
    AssertEq(c.hits, 0);
}

[[cccc::test]] void test_all_defaulted_takes_no_arguments(void) {
    struct Window w = Window_new();
    AssertEq(w.w, 80);
    AssertEq(w.h, 24);
}

[[cccc::test]] void test_typedef_target_and_array_parameter(void) {
    Item i = Item_new(7, "abc");
    AssertEq(i.id, 7);
    AssertEq(strcmp(i.tag, "abc"), 0);
}

@ctor("app") struct Point {
    int x;
    int y @ default(2);
};

[[cccc::test]] void test_prefix_names_the_constructor(void) {
    struct Point p = app_Point_new(1);
    AssertEq(p.x, 1);
    AssertEq(p.y, 2);
}

static int initial_retries(void) {
    return 4;
}

@ctor struct Init {
    int   port = DEFAULT_PORT;
    int   retries = initial_retries();
    char *label = "untitled";
    int   flags : 3 = 2;
    struct {
        int y = -1;
    };
    int n;
};

[[cccc::test]] void test_member_initializers_default_the_constructor(void) {
    struct Init i = Init_new(5);
    AssertEq(i.port, 8080);
    AssertEq(i.retries, 4);
    AssertEq(strcmp(i.label, "untitled"), 0);
    AssertEq(i.flags, 2);
    AssertEq(i.y, -1);
    AssertEq(i.n, 5);
}

[[cccc::test]] void test_member_initializers_leave_c_initialization_alone(void) {
    struct Init z = {0};
    AssertEq(z.port, 0);
    AssertEq(z.y, 0);
}

struct Late {
    int a = 1;
} @ctor;

typedef struct {
    int b = 2;
} Aliased @ctor;

[[cccc::test]] void test_ctor_after_the_body_accepts_initializers(void) {
    AssertEq(Late_new().a, 1);
    AssertEq(Aliased_new().b, 2);
}
