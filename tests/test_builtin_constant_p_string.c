// A string literal, with or without a cast, is a constant for
// __builtin_constant_p; the address of an object is not.
int g;

_Static_assert(__builtin_constant_p("x"), "string literal");
_Static_assert(__builtin_constant_p((const char *)"x"), "cast string literal");
_Static_assert(__has_builtin(__builtin_os_log_format), "os_log_format");
_Static_assert(__has_builtin(__builtin_os_log_format_buffer_size),
               "os_log_format_buffer_size");

int main(void) {
    if (!__builtin_constant_p("x"))
        return 1;
    if (__builtin_constant_p(&g))
        return 2;
    return 42;
}
