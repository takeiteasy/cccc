#include "fs.h"
#include <dirent.h>
#include <glob.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

char *fs_read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    size_t cap = 4096, n = 0;
    char  *buf = tool_xmalloc(cap);
    for (;;) {
        size_t got  = fread(buf + n, 1, cap - n - 1, f);
        n          += got;
        if (got == 0)
            break;
        if (n + 1 >= cap) {
            cap *= 2;
            buf  = tool_xrealloc(buf, cap);
        }
    }
    fclose(f);
    buf[n] = '\0';
    if (len)
        *len = n;
    return buf;
}

char *fs_read_file_or_die(const char *path, size_t *len) {
    char *s = fs_read_file(path, len);
    if (!s)
        tool_die("cannot read %s", path);
    return s;
}

char *fs_read_text_or_die(const char *path, size_t *len) {
    size_t n;
    char  *s = fs_read_file_or_die(path, &n);
    size_t w = 0;
    for (size_t r = 0; r < n; r++) {
        if (s[r] == '\r') {
            s[w++] = '\n';
            if (r + 1 < n && s[r + 1] == '\n')
                r++;
        } else {
            s[w++] = s[r];
        }
    }
    s[w] = '\0';
    if (len)
        *len = w;
    return s;
}

void fs_write_file(const char *path, const char *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f)
        tool_die("cannot write %s", path);
    if (len && fwrite(data, 1, len, f) != len)
        tool_die("short write to %s", path);
    if (fclose(f) != 0)
        tool_die("cannot close %s", path);
}

int fs_file_equals(const char *path, const char *data, size_t len) {
    size_t cur_len;
    char  *cur = fs_read_file(path, &cur_len);
    int    eq  = cur && cur_len == len && memcmp(cur, data, len) == 0;
    free(cur);
    return eq;
}

int fs_write_if_changed(const char *path, const char *data, size_t len) {
    if (fs_file_equals(path, data, len))
        return 0;
    fs_write_file(path, data, len);
    return 1;
}

int fs_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

int fs_is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

void strlist_push(StrList *l, const char *s) {
    l->v       = tool_xrealloc(l->v, (l->n + 1) * sizeof(char *));
    size_t n   = strlen(s) + 1;
    l->v[l->n] = tool_xmalloc(n);
    memcpy(l->v[l->n], s, n);
    l->n++;
}

void strlist_free(StrList *l) {
    for (size_t i = 0; i < l->n; i++)
        free(l->v[i]);
    free(l->v);
    l->v = NULL;
    l->n = 0;
}

static int strlist_cmp(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

void strlist_sort(StrList *l) {
    if (l->n)
        qsort(l->v, l->n, sizeof(char *), strlist_cmp);
}

StrList fs_glob(const char *pattern) {
    StrList l = {0};
    glob_t  g;
    memset(&g, 0, sizeof(g));
    if (glob(pattern, 0, NULL, &g) == 0)
        for (size_t i = 0; i < g.gl_pathc; i++)
            strlist_push(&l, g.gl_pathv[i]);
    globfree(&g);
    strlist_sort(&l);
    return l;
}

StrList fs_list_dir(const char *path) {
    StrList l = {0};
    DIR    *d = opendir(path);
    if (!d)
        return l;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        strlist_push(&l, e->d_name);
    }
    closedir(d);
    strlist_sort(&l);
    return l;
}

char *path_join(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    int    sep = la && a[la - 1] != '/';
    char  *out = tool_xmalloc(la + lb + 2);
    memcpy(out, a, la);
    if (sep)
        out[la++] = '/';
    memcpy(out + la, b, lb + 1);
    return out;
}

char *tool_repo_root(const char *tool_file) {
    char resolved[PATH_MAX];
    if (!realpath(tool_file, resolved))
        tool_die("cannot resolve %s", tool_file);
    // <root>/tools/x.c -> strip the file name, then "tools"
    for (int i = 0; i < 2; i++) {
        char *slash = strrchr(resolved, '/');
        if (!slash || slash == resolved)
            tool_die("unexpected tool path %s", tool_file);
        *slash = '\0';
    }
    size_t n   = strlen(resolved) + 1;
    char  *out = tool_xmalloc(n);
    memcpy(out, resolved, n);
    return out;
}
