// Array and anonymous-struct members: the getter returns a pointer to the
// first element, the setter and the constructor copy the whole array.

@generate_accessors("S") @generate_constructor struct S {
    int arr[4];
    int grid[2][3];
    struct {
        int  inner;
        char tag[2];
    };
    int plain;
};

int main(void) {
    int      vals[4]    = {1, 2, 3, 4};
    int      grid[2][3] = {{1, 2, 3}, {4, 5, 6}};
    char     tag[2]     = {'o', 'k'};
    struct S s          = S_create(vals, grid, 7, tag, 9);
    if (S_get_arr(&s)[2] != 3 || S_get_grid(&s)[1][2] != 6)
        return 1;
    if (S_get_inner(&s) != 7 || S_get_tag(&s)[1] != 'k' || S_get_plain(&s) != 9)
        return 2;

    int more[4] = {10, 20, 30, 40};
    S_set_arr(&s, more);
    if (s.arr[3] != 40)
        return 3;
    S_set_inner(&s, 70);
    if (s.inner != 70)
        return 4;
    return 42;
}
