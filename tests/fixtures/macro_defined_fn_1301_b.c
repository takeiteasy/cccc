// TU B for tests/test_serialize_macro_defined_fn_1301.c (#1301, negative
// guard). Only a plain declaration of thing_int -- never includes
// macro_defined_fn_1301_shared.h at all, so this TU has no way to see
// thing_int()'s body except through TU A's own definition text reaching
// the host compiler.
int thing_int(void);
int thing_caller_1301(void);

int main(void) {
    return thing_caller_1301() + thing_int();
}
