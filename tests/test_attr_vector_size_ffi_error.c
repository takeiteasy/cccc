// EXPECT_COMPILE_ERROR
// Vector-by-value through the native FFI marshalling path is not supported.
// Originally scoped out by #714; #721 (which added support for by-value
// vectors through variadic '...' params, see test_attr_vector_size_variadic.c)
// confirmed the FFI gap is a separate, harder problem -- CCCC's FFI path uses
// stock libffi, which has no portable vector ffi_type, and there is no
// struct-by-value support in the FFI path at all today to build on. Real
// support needs platform-specific ABI classification (see follow-up ticket
// #726). Must be rejected with a clear diagnostic rather than silently
// mis-marshalling.

typedef float v4sf __attribute__((vector_size(16)));

extern int strcmp(v4sf a);

int main(void) {
    v4sf a;
    a[0] = 1.0f;
    return strcmp(a);
}
