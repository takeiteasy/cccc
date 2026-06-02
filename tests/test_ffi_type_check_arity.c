// EXPECT_RUNTIME_ERROR JCC_FLAGS: --ffi-type-checking

int strcmp(const char *s);

int main(void) {
    return strcmp("missing second argument");
}
