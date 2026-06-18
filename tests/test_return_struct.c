// Tests for [[cccc::test(return = (struct T){...})]] — struct return-value
// assertion with compound-literal syntax (ticket #353).
// CCCC_FLAGS: --testing

struct Point { int x; int y; };

#pragma cccc suite begin "struct_return_basic"

// Basic: int fields match.
[[cccc::test(return = (struct Point){.x = 1, .y = 2})]]
struct Point test_point_eq(void) {
    return (struct Point){.x = 1, .y = 2};
}

// Larger field values.
[[cccc::test(return = (struct Point){.x = 100, .y = 200})]]
struct Point test_point_large(void) {
    return (struct Point){.x = 100, .y = 200};
}

// Negative int fields.
[[cccc::test(return = (struct Point){.x = -3, .y = -9})]]
struct Point test_point_negative(void) {
    return (struct Point){.x = -3, .y = -9};
}

// Unmentioned field defaults to 0 (C zero-init for expected value).
[[cccc::test(return = (struct Point){.x = 7})]]
struct Point test_point_partial(void) {
    return (struct Point){.x = 7, .y = 0};
}

// != operator: struct differs — assertion passes.
[[cccc::test(return != (struct Point){.x = 1, .y = 1})]]
struct Point test_point_ne(void) {
    return (struct Point){.x = 99, .y = 2};
}

// Combined with name= and suite= attribute args.
[[cccc::test(return = (struct Point){.x = 42, .y = 0},
             name = "point with name annotation")]]
struct Point test_point_named(void) {
    return (struct Point){.x = 42, .y = 0};
}

#pragma cccc suite end

// ---- Float / double fields ----

struct FPFields { float a; double b; };

[[cccc::test(return = (struct FPFields){.a = 1.5, .b = 3.14})]]
struct FPFields test_fp_fields(void) {
    return (struct FPFields){.a = 1.5f, .b = 3.14};
}

// Integer field alongside float — field comparison uses appropriate type rules.
struct Mixed { int code; double val; };

[[cccc::test(return = (struct Mixed){.code = 7, .val = 2.718})]]
struct Mixed test_mixed_int_double(void) {
    return (struct Mixed){.code = 7, .val = 2.718};
}

// ---- char* field (strcmp comparison) ----

struct Named { char *label; int code; };

[[cccc::test(return = (struct Named){.label = "hello", .code = 42})]]
struct Named test_named_struct(void) {
    return (struct Named){.label = "hello", .code = 42};
}

// ---- Single-field struct ----

struct Solo { int n; };

[[cccc::test(return = (struct Solo){.n = 99})]]
struct Solo test_solo(void) {
    return (struct Solo){.n = 99};
}

// ---- union keyword ----

union Val { int i; float f; };

[[cccc::test(return = (union Val){.i = 123})]]
union Val test_union_field(void) {
    return (union Val){.i = 123};
}
