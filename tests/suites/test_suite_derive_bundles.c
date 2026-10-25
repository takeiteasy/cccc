// CCCC_FLAGS: --testing
// Suite: the bundle attributes @json, @text, @binary, @lifecycle, @hashable and
// @ordered publish the functions of both of their parts, on the VM and under
// -c=native.

#include <cccc/derive.h>
#include <cccc/json.h>
#include <cccc/text.h>
#include <stdio.h>
#include <string.h>

@json @text @binary @lifecycle @hashable struct Item {
    int   id;
    char  name[8];
    char *note @owned;
};

@json("app") @text("app") @lifecycle("app") @hashable("app") struct Tagged {
    int a;
    int b;
};

@ordered struct Ranked {
    int rank;
};

@ordered("app") struct Tiered {
    int tier;
};

@text enum Mode { OFF, ON };

[[cccc::test]] void test_json_pair(void) {
    struct Item a = {1, "x", NULL}, b;
    char        buf[128];
    Item_to_json(&a, buf, sizeof buf);
    AssertEq(Item_from_json(&b, buf), 0);
    AssertEq(b.id, 1);
}

[[cccc::test]] void test_text_pair(void) {
    struct Item a = {2, "y", NULL}, b;
    char        buf[128];
    Item_to_string(&a, buf, sizeof buf);
    AssertEq(Item_from_string(&b, buf), 0);
    AssertEq(b.id, 2);
}

[[cccc::test]] void test_text_on_enum(void) {
    AssertEq(strcmp(Mode_to_string(ON), "ON"), 0);
    AssertEq((int)Mode_from_string("ON"), (int)ON);
}

[[cccc::test]] void test_binary_pair(void) {
    struct Item a = {3, "z", NULL}, b;
    char        buf[sizeof(struct Item)];
    AssertEq(Item_serialize(&a, buf), (int)sizeof(struct Item));
    b = Item_deserialize(buf);
    AssertEq(b.id, 3);
}

[[cccc::test]] void test_lifecycle_pair(void) {
    char        note[] = "n";
    struct Item a      = {4, "w", note}, b;
    AssertEq(Item_clone(&b, &a), 0);
    AssertEq(b.note != note, 1);
    Item_destroy(&b);
}

[[cccc::test]] void test_hashable_pair(void) {
    struct Item a = {5, "v", NULL}, b = a;
    AssertEq(Item_equals(&a, &b), 1);
    AssertEq(Item_hash(&a) == Item_hash(&b), 1);
}

[[cccc::test]] void test_ordered_pair(void) {
    struct Ranked a = {1}, b = {2};
    AssertEq(Ranked_equals(&a, &a), 1);
    AssertEq(Ranked_compare(&a, &b) < 0, 1);
}

[[cccc::test]] void test_prefix_reaches_both_parts(void) {
    struct Tagged a = {1, 2}, b;
    char          buf[128];
    app_Tagged_to_json(&a, buf, sizeof buf);
    AssertEq(app_Tagged_from_json(&b, buf), 0);
    app_Tagged_to_string(&a, buf, sizeof buf);
    AssertEq(app_Tagged_from_string(&b, buf), 0);
    AssertEq(app_Tagged_clone(&b, &a), 0);
    app_Tagged_destroy(&b);
    AssertEq(app_Tagged_equals(&a, &b), 1);
    AssertEq(app_Tagged_hash(&a) == app_Tagged_hash(&b), 1);
}

[[cccc::test]] void test_ordered_prefix(void) {
    struct Tiered a = {1}, b = {2};
    AssertEq(app_Tiered_equals(&a, &a), 1);
    AssertEq(app_Tiered_compare(&b, &a) > 0, 1);
}
