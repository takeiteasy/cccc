/*
 CCCC: Comprehensiev C Compensation Compiler

 Copyright (C) 2025 George Watson

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

// Host-side test for the --watch file watcher (src/watch.c): which events
// reach the queue, and how bursts are debounced.

#include "watch.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int  failed = 0;
static char dir[PATH_MAX];

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL (%s:%d): ", __FILE__, __LINE__);             \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
            failed = 1;                                                        \
        }                                                                      \
    } while (0)

static void path(char *out, const char *name) {
    snprintf(out, PATH_MAX + 64, "%s/%s", dir, name);
}

static void write_file(const char *name, const char *text) {
    char p[PATH_MAX + 64];
    path(p, name);
    FILE *f = fopen(p, "w");
    if (!f) {
        perror(p);
        exit(2);
    }
    fputs(text, f);
    fclose(f);
}

static void msleep(int ms) {
    usleep((useconds_t)ms * 1000);
}

// Waits for a settled burst and returns how many distinct paths it holds;
// `first` receives the first one's file name.
static int next_burst(char *first, size_t cap) {
    cc_watch_wait();
    int    n = 0;
    char **v = cc_watch_take(&n);
    first[0] = 0;
    if (n > 0) {
        const char *slash = strrchr(v[0], '/');
        snprintf(first, cap, "%s", slash ? slash + 1 : v[0]);
    }
    for (int i = 0; i < n; i++)
        free(v[i]);
    free(v);
    return n;
}

// Lets late events from the previous step arrive, then forgets them.
static void drain(void) {
    msleep(700);
    int    n;
    char **v = cc_watch_take(&n);
    for (int i = 0; i < n; i++)
        free(v[i]);
    free(v);
}

static void expect_one(const char *what, const char *name) {
    char first[PATH_MAX];
    int  n = next_burst(first, sizeof first);
    CHECK(n == 1, "%s: expected 1 changed path, got %d", what, n);
    CHECK(!strcmp(first, name), "%s: expected %s, got '%s'", what, name, first);
    drain();
}

static void remove_tree(const char *path) {
    DIR *d = opendir(path);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
                continue;
            char child[PATH_MAX + 300];
            snprintf(child, sizeof child, "%s/%s", path, e->d_name);
            struct stat st;
            if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode))
                remove_tree(child);
            else
                unlink(child);
        }
        closedir(d);
    }
    rmdir(path);
}

int main(void) {
    alarm(60);
    char tmpl[] = "/tmp/cccc_watch_test_XXXXXX";
    if (!mkdtemp(tmpl))
        return 2;
    realpath(tmpl, dir);
    char sub[PATH_MAX + 64];
    path(sub, "out");
    mkdir(sub, 0755);

    write_file("a.c", "1\n");
    write_file("k.c", "1\n");
    write_file("z.c", "1\n");
    write_file("out/o.c", "1\n");

    char          *argv[] = {"cccc", NULL};
    CcWatchOptions opt    = {.debounce_ms = 100, .argv = argv};
    CHECK(cc_watch_init(&opt), "watcher did not start");

    char p[PATH_MAX + 64];
    path(p, "a.c");
    cc_watch_add(p);
    path(p, "k.c");
    cc_watch_add(p);
    path(p, "out/o.c");
    cc_watch_add(p);
    cc_watch_ignore("k.c");
    path(p, "out");
    cc_watch_exclude(p);
    CHECK(cc_watch_arm(), "arm failed");
    CHECK(cc_watch_file_count() == 3, "expected 3 watched files");
    drain();

    write_file("a.c", "2\n");
    expect_one("in-place write", "a.c");

    // An editor that saves by writing a temp file and renaming it over the
    // name.
    write_file("a.c.tmp", "3\n");
    char from[PATH_MAX + 64];
    path(from, "a.c.tmp");
    path(p, "a.c");
    rename(from, p);
    expect_one("write-then-rename", "a.c");

    // A burst of writes is one change, listed once.
    for (int i = 0; i < 6; i++) {
        write_file("a.c", "burst\n");
        msleep(15);
    }
    expect_one("burst", "a.c");

    write_file("z.c", "2\n");     // not watched
    write_file("k.c", "2\n");     // ignored by glob
    write_file("out/o.c", "2\n"); // excluded directory
    msleep(600);
    write_file("a.c", "4\n");
    expect_one("unwatched, ignored and excluded files are dropped", "a.c");

    // A file created later that matches a glob is seen.
    snprintf(p, sizeof p, "%s/*.txt", dir);
    cc_watch_add_glob(p);
    CHECK(cc_watch_arm(), "arm after glob failed");
    drain();
    write_file("new.txt", "x\n");
    expect_one("glob", "new.txt");

    remove_tree(dir);
    if (!failed)
        printf("PASS: watch\n");
    return failed;
}
