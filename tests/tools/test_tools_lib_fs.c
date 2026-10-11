// tools/lib/fs: read/write, write-if-changed, glob, list_dir, path_join,
// tool_repo_root.
#include "../../tools/lib/fs.c"
#include "../../tools/lib/str.c"
#include <unistd.h>

#define CHECK(c)                                                               \
    do {                                                                       \
        if (!(c)) {                                                            \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,   \
                    #c);                                                       \
            return 1;                                                          \
        }                                                                      \
    } while (0)

int main(void) {
    char dir[] = "/tmp/cccc_tools_fs_XXXXXX";
    CHECK(mkdtemp(dir) != NULL);

    char *a = path_join(dir, "a.txt");
    char *b = path_join(dir, "b.txt");
    char *c = path_join(dir, "c.dat");
    CHECK(strcmp(path_join("x/", "y"), "x/y") == 0);
    CHECK(strcmp(path_join("x", "y"), "x/y") == 0);

    CHECK(!fs_exists(a));
    CHECK(fs_read_file(a, NULL) == NULL);
    CHECK(fs_write_if_changed(a, "hello\n", 6) == 1);
    CHECK(fs_write_if_changed(a, "hello\n", 6) == 0);
    CHECK(fs_write_if_changed(a, "hello!\n", 7) == 1);
    CHECK(fs_exists(a));
    CHECK(fs_file_equals(a, "hello!\n", 7));
    CHECK(!fs_file_equals(a, "hello!", 6));
    CHECK(!fs_file_equals(b, "", 0));

    size_t len = 0;
    char  *got = fs_read_file_or_die(a, &len);
    CHECK(len == 7 && strcmp(got, "hello!\n") == 0);
    free(got);

    // a large file survives the grow loop
    StrBuf big = {0};
    for (int i = 0; i < 5000; i++)
        sb_append(&big, "0123456789abcdef");
    fs_write_file(b, big.data, big.len);
    got = fs_read_file_or_die(b, &len);
    CHECK(len == big.len && memcmp(got, big.data, len) == 0);
    free(got);
    sb_free(&big);

    fs_write_file(c, "", 0);
    got = fs_read_file_or_die(c, &len);
    CHECK(len == 0 && got[0] == '\0');
    free(got);

    fs_write_file(c, "a\r\nb\rc\n", 7);
    got = fs_read_text_or_die(c, &len);
    CHECK(len == 6 && strcmp(got, "a\nb\nc\n") == 0);
    free(got);
    fs_write_file(c, "", 0);

    CHECK(fs_is_dir(dir) && !fs_is_dir(a));

    char   *pat = path_join(dir, "*.txt");
    StrList g   = fs_glob(pat);
    CHECK(g.n == 2);
    CHECK(strcmp(g.v[0], a) == 0 && strcmp(g.v[1], b) == 0);
    strlist_free(&g);
    pat = path_join(dir, "*.nope");
    g   = fs_glob(pat);
    CHECK(g.n == 0);

    StrList d = fs_list_dir(dir);
    CHECK(d.n == 3);
    CHECK(strcmp(d.v[0], "a.txt") == 0 && strcmp(d.v[1], "b.txt") == 0 &&
          strcmp(d.v[2], "c.dat") == 0);
    strlist_free(&d);
    d = fs_list_dir("/nonexistent/dir/for/cccc");
    CHECK(d.n == 0);

    // this test lives at <root>/tests/tools/x.c, so two levels up is
    // <root>/tests
    char *root  = tool_repo_root(__FILE__);
    char *probe = path_join(root, "../tools/lib/fs.c");
    CHECK(fs_exists(probe));

    unlink(a);
    unlink(b);
    unlink(c);
    rmdir(dir);
    return 42;
}
