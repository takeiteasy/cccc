// CCCC_NATIVE_SKIP: -c=native rejects FFI policy options
// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --ffi-type-checking

int strcmp(const char *s);

int main(void) {
    return strcmp("missing second argument");
}
