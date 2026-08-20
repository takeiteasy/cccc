// Tests __attribute__((constructor(priority))) ordering: lower priority
// numbers run first; constructors with no explicit priority form the
// default group and run last.

int order[4];
int idx = 0;

__attribute__((constructor(200))) void c200(void) {
    order[idx++] = 200;
}
__attribute__((constructor(101))) void c101(void) {
    order[idx++] = 101;
}
__attribute__((constructor)) void cdefault(void) {
    order[idx++] = -1;
}

int main(void) {
    if (idx != 3)
        return 1;
    if (order[0] != 101)
        return 1; // lowest priority number first
    if (order[1] != 200)
        return 1;
    if (order[2] != -1)
        return 1; // default-priority group runs last
    return 42;
}
