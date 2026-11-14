// CCCC_FLAGS: --testing
// Suite: derived functions are also methods, run on the VM and under -c=native.

#include <cccc/derive.h>
#include <stdlib.h>
#include <string.h>

@accessors
@ctor
@equals
@hash
@compare
@to_json
@from_json
@to_string
@from_string
@serialize
@deserialize
@validate
@clamp
typedef struct Pt {
    int x @range(0, 10);
    int y;
} Pt;

@clone
@destroy
typedef struct Doc {
    char *title @owned;
    int   n;
} Doc;

@equals("app")
@hash("app")
typedef struct Tagged {
    int a;
} Tagged;

@equals
struct Plain {
    int a;
};

@accessors
typedef union Num {
    int   i;
    float f;
} Num;

@template(T)
@equals
struct Cell {
    T a;
    T b;
};

[[cccc::test]] void test_accessors_and_new(void) {
    Pt p = Pt.new(3, 4);
    AssertEq(p.get_x(), 3);
    p.set_x(5);
    AssertEq(Pt_get_x(&p), 5);
    p.set_y(9);
    AssertEq(p.get_y(), 9);
}

[[cccc::test]] void test_equals_hash_compare(void) {
    Pt a = Pt.new(1, 2);
    Pt b = Pt.new(1, 2);
    Pt c = Pt.new(2, 0);
    AssertEq(!!a.equals(&b), 1);
    AssertEq(!!a.equals(&c), 0);
    AssertEq(!!(a.hash() == b.hash()), 1);
    AssertEq(a.compare(&b), 0);
    AssertEq(a.compare(&c) < 0, 1);
    AssertEq(c.compare(&a) > 0, 1);
}

[[cccc::test]] void test_json_and_string_round_trip(void) {
    Pt   p = Pt.new(3, 4);
    Pt   q = Pt.new(0, 0);
    char buf[128];
    AssertEq(p.to_json(buf, sizeof buf), strlen(buf));
    AssertEq(q.from_json(buf), 0);
    AssertEq(!!q.equals(&p), 1);

    Pt r = Pt.new(0, 0);
    AssertEq(p.to_string(buf, sizeof buf), strlen(buf));
    AssertEq(r.from_string(buf), 0);
    AssertEq(!!r.equals(&p), 1);
}

[[cccc::test]] void test_serialize_family(void) {
    Pt   p = Pt.new(3, 4);
    char bytes[64];
    AssertEq(p.serialize_size(), sizeof(Pt));
    AssertEq(p.serialize(bytes), (int)sizeof(Pt));

    Pt a = Pt.deserialize(bytes);
    AssertEq(!!a.equals(&p), 1);

    Pt b = Pt.new(0, 0);
    AssertEq(b.deserialize_read(bytes), sizeof(Pt));
    AssertEq(!!b.equals(&p), 1);

    Pt c = Pt.new(0, 0);
    AssertEq(c.deserialize_into(bytes, sizeof bytes), 0);
    AssertEq(!!c.equals(&p), 1);
    AssertEq(c.deserialize_into(bytes, 1), 1);
}

[[cccc::test]] void test_validate_and_clamp(void) {
    Pt p = {99, 0};
    AssertEq(!!p.validate(), 0);
    p.clamp();
    AssertEq(p.x, 10);
    AssertEq(!!p.validate(), 1);
}

[[cccc::test]] void test_clone_and_destroy(void) {
    Doc d = {malloc(3), 1};
    strcpy(d.title, "hi");
    Doc c;
    AssertEq(c.clone(&d), 0);
    AssertEq(strcmp(c.title, "hi"), 0);
    AssertEq(c.title != d.title, 1);
    c.destroy();
    d.destroy();
    AssertEq(c.title == NULL, 1);
    AssertEq(d.title == NULL, 1);
}

[[cccc::test]] void test_prefix_does_not_change_method_name(void) {
    Tagged a = {1};
    Tagged b = {1};
    AssertEq(!!app_Tagged_equals(&a, &b), 1);
    AssertEq(!!a.equals(&b), 1);
    AssertEq(a.hash() == app_Tagged_hash(&a), 1);
}

[[cccc::test]] void test_tag_only_struct(void) {
    struct Plain a = {1};
    struct Plain b = {2};
    AssertEq(!!a.equals(&b), 0);
    b.a = 1;
    AssertEq(!!a.equals(&b), 1);
}

[[cccc::test]] void test_union_accessors(void) {
    Num n = {0};
    n.set_i(7);
    AssertEq(n.get_i(), 7);
}

[[cccc::test]] void test_template_instance(void) {
    @inst(int) Cell x = {1, 2};
    @inst(int) Cell y = {1, 2};
    @inst(int) Cell z = {1, 3};
    AssertEq(!!x.equals(&y), 1);
    AssertEq(!!x.equals(&z), 0);
}
