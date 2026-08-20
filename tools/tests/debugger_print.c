// Fixture for the debugger `print`/`p` command's PTY integration tests
// (#958, see tools/test_debugger_print.py). Exercises cc_dump_value on a
// live paused frame: a plain struct, a nested struct, an array, a char*
// string, a pointer to another local, and a data-segment global -- the
// mix of address kinds (stack frame + data segment) that #958's
// cc_is_valid_vm_address stack-bound fix and the new `print` command both
// need to get right.

struct Point {
    int x;
    int y;
};
struct Line {
    struct Point a;
    struct Point b;
};

int global_counter = 7;

int main(void) {
    struct Point p      = {3, 4};
    struct Line  ln     = {{1, 2}, {3, 4}};
    int          arr[4] = {10, 20, 30, 40};
    char        *s      = "hi";
    int          local  = 42;
    int         *pl     = &local;

    int          ok = local == 42 && p.x == 3 && ln.b.y == 4 && arr[2] == 30 &&
                      s[0] == 'h' && *pl == 42 && global_counter == 7;
    return ok ? 42 : 1;
}
