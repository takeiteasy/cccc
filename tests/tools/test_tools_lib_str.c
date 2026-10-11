// tools/lib/str: StrBuf, strip, split-on-newline.
#include "../../tools/lib/str.c"
#include <assert.h>

#define CHECK(c)                                                               \
    do {                                                                       \
        if (!(c)) {                                                            \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,   \
                    #c);                                                       \
            return 1;                                                          \
        }                                                                      \
    } while (0)

int main(void) {
    StrBuf sb = {0};
    sb_append(&sb, "ab");
    sb_append_n(&sb, "cdef", 2);
    sb_appendf(&sb, "-%d-%s", 42, "x");
    CHECK(sb.len == 9);
    CHECK(strcmp(sb.data, "abcd-42-x") == 0);
    for (int i = 0; i < 1000; i++)
        sb_append(&sb, "0123456789");
    CHECK(sb.len == 9 + 10000);
    CHECK(sb.data[sb.len] == '\0');
    sb_free(&sb);
    CHECK(sb.data == NULL && sb.len == 0);

    CHECK(str_starts_with("//>>> shim", "//>>>"));
    CHECK(!str_starts_with("//", "//>>>"));
    CHECK(str_starts_with("x", ""));

    CHECK(str_is_blank(""));
    CHECK(str_is_blank(" \t\r\n"));
    CHECK(!str_is_blank(" x "));

    char *s = str_strip_dup(" \t hello world \n");
    CHECK(strcmp(s, "hello world") == 0);
    free(s);
    s = str_strip_dup("   ");
    CHECK(strcmp(s, "") == 0);
    free(s);

    Lines l = str_split_lines("a\nb\n");
    CHECK(l.n == 3);
    CHECK(strcmp(l.v[0], "a") == 0 && strcmp(l.v[1], "b") == 0 &&
          strcmp(l.v[2], "") == 0);
    lines_free(&l);
    l = str_split_lines("");
    CHECK(l.n == 1 && strcmp(l.v[0], "") == 0);
    lines_free(&l);
    l = str_split_lines("\n\nx");
    CHECK(l.n == 3 && strcmp(l.v[2], "x") == 0 && l.v[0][0] == '\0');
    lines_free(&l);
    return 42;
}
