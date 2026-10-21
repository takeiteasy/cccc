// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int main\(void\)
// CCCC_EXPECT_STDOUT: static int __cccc_[A-Za-z0-9_]*arr[A-Za-z0-9_]*\[3\]
// CCCC_EXPECT_STDOUT: static struct S __cccc_[A-Za-z0-9_]+ = \{ \.a = 1, \.b = 2 \}
// CCCC_EXPECT_STDOUT: shadow_param\(int x\)(.|\n)*shadow_param\(int x\)
// CCCC_REJECT_STDOUT: \.L\.\.
// CCCC_REJECT_STDOUT: unsupported expr
// CCCC_REJECT_STDOUT: int ([A-Za-z0-9_]+);\n[ \t]+int \1;
// -m output has no dotted names, no unsupported-expr placeholder, no duplicate
// local declarations, and keeps parameter names.
struct S {
    int a;
    int b;
};

static int *get_static_array(void) {
    static int arr[3];
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 12;
    return arr;
}

int      *gp = (int[]){10, 20, 12};
struct S *gs = &(struct S){1, 2};

static int shadow_param(int x) {
    {
        int x = 5;
        return x;
    }
}

static int sum_loops(void) {
    int total = 0;
    for (int i = 0; i < 3; i++) {
        total += i;
    }
    for (int i = 0; i < 4; i++) {
        total += i * 10;
    }
    for (int i = 0, j = 10; i < 2; i++, j--) {
        total += j;
    }
    for (int i = 0; i < 2; i++) {
        for (int i = 0; i < 2; i++) {
            total += 1;
        }
    }
    return total;
}

int main(void) {
    int *arr1 = get_static_array();
    if (arr1[0] != 10 || arr1[1] != 20 || arr1[2] != 12)
        return 1;
    int *arr2 = get_static_array();
    if (arr2[0] != 10)
        return 2;
    if (gp[0] != 10 || gp[1] != 20 || gp[2] != 12)
        return 3;
    if (gs->a != 1 || gs->b != 2)
        return 4;
    if (sum_loops() != 86)
        return 5;
    if (shadow_param(1) != 5)
        return 6;
    return 42;
}
