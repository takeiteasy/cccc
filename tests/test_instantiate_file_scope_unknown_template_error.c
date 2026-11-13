// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: InstantiateType: 'nothing' is not a template
@template(T) struct Box {
    T value;
};

@comptime void gen(void) {
    InstantiateType("nothing", "int");
}
gen();

int main(void) {
    return 0;
}
