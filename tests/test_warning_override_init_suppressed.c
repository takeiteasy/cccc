// CCCC_FLAGS: -Woverride-init -Wno-override-init
// CCCC_REJECT_STDERR: warning:

struct S { int x; int y; };

int main(void) {
    struct S s = { .x = 1, .y = 5, .x = 42 };
    return s.x;
}
