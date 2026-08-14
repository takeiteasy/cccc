// Fixture TU1 for tests/test_serialize_static_name_collision.c (#1002).
static int collide_1002(void) { return 20; }
int collide_1002_call_a(void) { return collide_1002(); }
