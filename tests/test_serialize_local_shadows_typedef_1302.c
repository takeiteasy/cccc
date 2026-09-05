// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int local_shadows_typedef_1302_Widget__cccc_\d+;
// CCCC_REJECT_STDOUT: \n {4}int local_shadows_typedef_1302_Widget;\n
//
// #1302 (typedef variant): same root cause as
// test_serialize_local_shadows_global_1302.c -- serialize_function()
// (src/serialize_decl.c) hoists every local of a function to one flat
// top-of-function declaration, widening a block-scoped local's scope to
// the whole function. Here the colliding name is a file-scope *typedef*
// rather than a global function/variable:
// `local_shadows_typedef_1302_Widget` names both the typedef and a
// block-scoped `int` local in the same function, which also declares an
// ordinary `Widget w` outside that block. Pre-fix, hoisting `int
// local_shadows_typedef_1302_Widget;` to the top of the function shadowed
// the typedef for the rest of the function, so `local_shadows_typedef_1302_
// Widget w;`'s declarator read as two consecutive expression-statements
// (`local_shadows_typedef_1302_Widget` and `w`, the latter undeclared) --
// several host compile errors.
//
// Fixed by the same widened #926 collision loop as the global variant,
// checked here unconditionally against every file-scope typedef name (no
// cheap "is this typedef spelled again later" test exists the way a
// referenced-global check has one) -- see that loop's own #1302 comment in
// src/serialize_decl.c.
typedef struct {
    int a;
} local_shadows_typedef_1302_Widget;

int local_shadows_typedef_1302_f(int flag) {
    if (flag) {
        int local_shadows_typedef_1302_Widget = 5;
        return local_shadows_typedef_1302_Widget;
    }
    local_shadows_typedef_1302_Widget w;
    w.a = 42;
    return w.a;
}

int main(void) {
    return local_shadows_typedef_1302_f(0);
}
