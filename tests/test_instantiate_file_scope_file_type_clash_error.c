// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a file-scope call made 'Box__typedef_Point' over its own copy
typedef struct {
    int a;
} Point;

@template(T) struct Box {
    T value;
};

@comptime void gen(void) {
    InstantiateType("Box", "Point");
}
gen();

int main(void) {
    Box @(Point) p = {{1}};
    return p.value.a;
}
