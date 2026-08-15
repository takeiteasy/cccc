// CCCC_FLAGS: tests/fixtures/dup_enum_1015_typedef_a.c tests/fixtures/dup_enum_1015_typedef_b.c -m
// CCCC_C4_SKIP: multi-source compile, not a single-TU bytecode round-trip
// CCCC_EXPECT_STDOUT: (?=[\s\S]*typedef enum \{\n    AA1015TD = 1,)(?=[\s\S]*typedef enum \{\n    AA1015TD__cccc_dup[0-9]+ = 5,)
// CCCC_REJECT_STDOUT: ^    AA1015TD = 5,$
//
// #1015 widening: a tagless typedef'd enum (`typedef enum { ... } T1015A;`)
// never forms a group in rename_colliding_type_tags() (#1014), since that
// pass only walks ctx->tags (src/serialize.c) -- a tagless enum has no tag
// record at all. rename_colliding_enum_constants() collects candidate
// enum groups from ctx->typedefs as well as ctx->tags, so this shape's
// colliding AA1015TD is still caught.
//
// Verified this exact program printed `AA1015TD` unrenamed under both
// typedef'd enums before this fix -- a host "redefinition of enumerator"
// compile error.
extern int a_use_1015td(void);
extern int b_use_1015td(void);

int main(void) {
    return a_use_1015td() + b_use_1015td() + 28;
}
