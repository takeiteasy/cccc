// CCCC_FLAGS: tests/fixtures/collision_1002_a.c
// Two inputs each defining a static function of the same name compile and link
// under -c=native.
int collide_1002_call_a(void);
static int collide_1002(void) {
    return 22;
}
int main(void) {
    return collide_1002_call_a() + collide_1002();
}
