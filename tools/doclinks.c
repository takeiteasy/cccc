// doclinks.c -- check the relative Markdown links in README.md and docs/*.md.
//
// A link target must name an existing file or directory (exact case, so a link
// that works on macOS fails here as it would on GitHub) and, for a `.md`
// target with a `#fragment`, a heading whose GitHub slug is that fragment.
//
// Usage: cc -o doclinks tools/doclinks.c && ./doclinks   (from the repo root)
// Exits 1 and lists each broken link, or 0 when every link resolves.
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define PATH_MAX_LEN 1024

static int broken;

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char  *buf = malloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got]   = 0;
    fclose(f);
    return buf;
}

// Exact-case existence: each path component must match a directory entry.
static int exists_exact(const char *path) {
    char cur[PATH_MAX_LEN] = ".";
    char tmp[PATH_MAX_LEN];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *comp = strtok(tmp, "/"); comp; comp = strtok(NULL, "/")) {
        DIR *d = opendir(cur);
        if (!d)
            return 0;
        int found = 0;
        for (struct dirent *e; (e = readdir(d));)
            if (!strcmp(e->d_name, comp)) {
                found = 1;
                break;
            }
        closedir(d);
        if (!found)
            return 0;
        size_t len = strlen(cur);
        snprintf(cur + len, sizeof cur - len, "/%s", comp);
    }
    return 1;
}

// Collapse "." and ".." components of dir/rel into out.
static void join_path(char *out, size_t cap, const char *dir, const char *rel) {
    char tmp[PATH_MAX_LEN], *parts[128];
    int  n = 0;
    snprintf(tmp, sizeof tmp, "%s/%s", dir, rel);
    for (char *c = strtok(tmp, "/"); c && n < 128; c = strtok(NULL, "/")) {
        if (!strcmp(c, ".") || !*c)
            continue;
        if (!strcmp(c, "..")) {
            if (n)
                n--;
            continue;
        }
        parts[n++] = c;
    }
    out[0] = 0;
    for (int i = 0; i < n; i++) {
        size_t len = strlen(out);
        snprintf(out + len, cap - len, "%s%s", i ? "/" : "", parts[i]);
    }
}

static void slugify(const char *h, char *out, size_t cap) {
    size_t o = 0;
    for (; *h && o + 1 < cap; h++) {
        unsigned char c = (unsigned char)*h;
        if (c == '`')
            continue;
        if (c == ' ')
            out[o++] = '-';
        else if (isalnum(c) || c == '_' || c == '-')
            out[o++] = (char)tolower(c);
    }
    out[o] = 0;
}

static int in_fence(const char *line, int fence) {
    return strncmp(line, "```", 3) == 0 ? !fence : fence;
}

// Is `want` the slug of some heading in `path` (counting -1, -2 repeats)?
static int has_anchor(const char *path, const char *want) {
    char *text = read_file(path);
    if (!text)
        return 0;
    char slugs[2048][96];
    int  nslugs = 0, found = 0, fence = 0;
    for (char *line = strtok(text, "\n"); line && !found;
         line       = strtok(NULL, "\n")) {
        int was = fence;
        fence   = in_fence(line, fence);
        if (was || fence || line[0] != '#')
            continue;
        const char *h = line;
        while (*h == '#')
            h++;
        if (*h != ' ' || nslugs >= 2048)
            continue;
        char base[96], slug[110];
        slugify(h + 1, base, sizeof base);
        int dup = 0;
        for (int i = 0; i < nslugs; i++)
            if (!strcmp(slugs[i], base))
                dup++;
        snprintf(slugs[nslugs++], 96, "%s", base);
        if (dup)
            snprintf(slug, sizeof slug, "%s-%d", base, dup);
        else
            snprintf(slug, sizeof slug, "%s", base);
        found = !strcmp(slug, want);
    }
    free(text);
    return found;
}

static void check_target(const char *file, int lineno, const char *target) {
    if (strstr(target, "://") || !strncmp(target, "mailto:", 7))
        return;
    char pathpart[PATH_MAX_LEN], resolved[PATH_MAX_LEN], dir[PATH_MAX_LEN];
    const char *hash = strchr(target, '#');
    size_t      plen = hash ? (size_t)(hash - target) : strlen(target);
    snprintf(pathpart, sizeof pathpart, "%.*s", (int)plen, target);
    snprintf(dir, sizeof dir, "%s", file);
    char *slash = strrchr(dir, '/');
    if (slash)
        *slash = 0;
    else
        dir[0] = 0;
    if (plen)
        join_path(resolved, sizeof resolved, dir, pathpart);
    else
        snprintf(resolved, sizeof resolved, "%s", file);
    if (plen && !exists_exact(resolved)) {
        printf("%s:%d: missing file %s\n", file, lineno, target);
        broken++;
        return;
    }
    size_t rlen = strlen(resolved);
    if (hash && hash[1] && rlen > 3 && !strcmp(resolved + rlen - 3, ".md") &&
        !has_anchor(resolved, hash + 1)) {
        printf("%s:%d: missing anchor %s\n", file, lineno, target);
        broken++;
    }
}

static void check_file(const char *file) {
    char *text = read_file(file);
    if (!text)
        return;
    int   fence = 0, lineno = 0;
    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line;
         line       = strtok_r(NULL, "\n", &save)) {
        lineno++;
        int was = fence;
        fence   = in_fence(line, fence);
        if (was || fence)
            continue;
        int code = 0;
        for (char *p = line; *p; p++) {
            if (*p == '`') {
                code = !code;
                continue;
            }
            if (code || p[0] != ']' || p[1] != '(')
                continue;
            char   target[PATH_MAX_LEN];
            size_t n = strcspn(p + 2, ") \t");
            snprintf(target, sizeof target, "%.*s", (int)n, p + 2);
            check_target(file, lineno, target);
        }
    }
    free(text);
}

int main(void) {
    check_file("README.md");
    DIR *d = opendir("docs");
    if (!d) {
        fprintf(stderr, "doclinks: run from the repository root\n");
        return 2;
    }
    for (struct dirent *e; (e = readdir(d));) {
        size_t n = strlen(e->d_name);
        if (n > 3 && !strcmp(e->d_name + n - 3, ".md")) {
            char path[PATH_MAX_LEN];
            snprintf(path, sizeof path, "docs/%s", e->d_name);
            check_file(path);
        }
    }
    closedir(d);
    printf("%d broken\n", broken);
    return broken != 0;
}
