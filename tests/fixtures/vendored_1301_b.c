// TU B for tests/test_serialize_vendored_single_header_1301.c (#1301). Only
// a plain declaration of v1301_add -- never re-includes
// vendored_1301_lib.h at all, so this TU never even sees
// VENDORED_1301_IMPLEMENTATION's own definition text, only the function
// itself supplied by TU A's replayed #include.
int v1301_add(int x);
int vendored_1301_call_a(void);

int main(void) {
    return vendored_1301_call_a() + v1301_add(20);
}
