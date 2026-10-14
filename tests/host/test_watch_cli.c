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

// End-to-end test of `cccc --watch`: runs the real binary on temp programs,
// edits them and reads what it prints. The binary is $CCCC_BIN, else
// build/cccc, else ./cccc; the test is skipped when none exists.

#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int  failed = 0;
static char cccc[PATH_MAX];
static char root[PATH_MAX];

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL (%s:%d): ", __FILE__, __LINE__);             \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
            failed = 1;                                                        \
        }                                                                      \
    } while (0)

typedef struct {
    pid_t  pid;
    int    fd;
    char   buf[1 << 16];
    size_t len;
} Child;

static void write_file(const char *dir, const char *name, const char *text) {
    char p[PATH_MAX + 64];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "w");
    if (!f) {
        perror(p);
        exit(2);
    }
    fputs(text, f);
    fclose(f);
}

static void spawn(Child *c, const char *dir, char *const argv[]) {
    int pipefd[2];
    if (pipe(pipefd) != 0)
        exit(2);
    c->pid = fork();
    if (c->pid == 0) {
        if (chdir(dir) != 0)
            _exit(127);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execv(cccc, argv);
        _exit(127);
    }
    close(pipefd[1]);
    c->fd  = pipefd[0];
    c->len = 0;
}

static void reap(Child *c) {
    kill(c->pid, SIGKILL);
    waitpid(c->pid, NULL, 0);
    close(c->fd);
}

// Waits until the output after `from` contains `needle`. Returns the offset
// just past it, or -1 on timeout.
static long wait_for(Child *c, const char *needle, size_t from, int ms) {
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        c->buf[c->len] = 0;
        if (from <= c->len) {
            char *hit = strstr(c->buf + from, needle);
            if (hit)
                return (long)(hit - c->buf) + (long)strlen(needle);
        }
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long left = ms - ((now.tv_sec - t0.tv_sec) * 1000 +
                          (now.tv_nsec - t0.tv_nsec) / 1000000);
        if (left <= 0)
            return -1;
        struct pollfd p = {.fd = c->fd, .events = POLLIN};
        if (poll(&p, 1, (int)left) > 0) {
            ssize_t n =
                read(c->fd, c->buf + c->len, sizeof c->buf - c->len - 1);
            if (n > 0)
                c->len += (size_t)n;
            else if (n == 0)
                return -1;
        }
    }
}

#define EXPECT(c, needle, from, what)                                          \
    ({                                                                         \
        long at_ = wait_for(c, needle, from, 15000);                           \
        CHECK(at_ >= 0, "%s: never saw '%s'; output:\n%s", what, needle,       \
              (c)->buf);                                                       \
        at_ < 0 ? (long)(c)->len : at_;                                        \
    })

static const char *LOOP_F = "#include <stdio.h>\n#include <unistd.h>\n"
                            "int f(void) { return %d; }\n"
                            "int main(void) {\n"
                            "    for (int i = 0; i < 400; i++) {\n"
                            "        printf(\"f=%%d\\n\", f());\n"
                            "        fflush(stdout);\n"
                            "        usleep(50000);\n"
                            "    }\n"
                            "    return 0;\n"
                            "}\n";

static void test_hot_reload(const char *dir) {
    char src[2048];
    snprintf(src, sizeof src, LOOP_F, 1);
    write_file(dir, "p.c", src);
    char *argv[] = {cccc, "--watch", "p.c", NULL};
    Child c;
    spawn(&c, dir, argv);
    long at = EXPECT(&c, "f=1\n", 0, "hot reload: first run");
    snprintf(src, sizeof src, LOOP_F, 2);
    write_file(dir, "p.c", src);
    at = EXPECT(&c, "1 changed", (size_t)at, "hot reload: reload message");
    EXPECT(&c, "f=2\n", (size_t)at, "hot reload: new code runs");
    reap(&c);
}

static void test_refused_reload(const char *dir) {
    const char *v1 =
        "#include <stdio.h>\n#include <unistd.h>\nint f(int x) { return x; }\n"
        "int main(void) {\n"
        "    for (int i = 0; i < 400; i++) {\n"
        "        printf(\"v=%d\\n\", f(1));\n"
        "        fflush(stdout);\n"
        "        usleep(50000);\n"
        "    }\n"
        "    return 0;\n"
        "}\n";
    write_file(dir, "r.c", v1);
    char *argv[] = {cccc, "--watch", "r.c", NULL};
    Child c;
    spawn(&c, dir, argv);
    long at = EXPECT(&c, "v=1\n", 0, "refused reload: first run");
    // A signature change is refused; the old code keeps running.
    const char *v2 =
        "#include <stdio.h>\n"
        "#include <unistd.h>\nint f(int x, int y) { return x + y; }\n"
        "int main(void) {\n"
        "    for (int i = 0; i < 400; i++) {\n"
        "        printf(\"v=%d\\n\", f(1, 5));\n"
        "        fflush(stdout);\n"
        "        usleep(50000);\n"
        "    }\n"
        "    return 0;\n"
        "}\n";
    write_file(dir, "r.c", v2);
    at = EXPECT(&c, "error", (size_t)at, "refused reload: error reported");
    EXPECT(&c, "v=1\n", (size_t)at, "refused reload: old code keeps running");
    reap(&c);
}

// Two files share a header; saving a header reloads the file that includes
// it, also a header that file only started to include after an earlier reload.
static void test_header_reload(const char *dir) {
    write_file(dir, "h_main.c",
               "#include <stdio.h>\n#include <unistd.h>\n"
               "int fb(void);\n"
               "int main(void) {\n"
               "    for (int i = 0; i < 400; i++) {\n"
               "        printf(\"fb=%d\\n\", fb());\n"
               "        fflush(stdout);\n"
               "        usleep(50000);\n"
               "    }\n"
               "    return 0;\n"
               "}\n");
    write_file(dir, "h_b.h", "#define B 1\n");
    write_file(dir, "h_c.h", "#define C 10\n");
    write_file(dir, "h_b.c", "#include \"h_b.h\"\nint fb(void) { return B; }\n");
    char *argv[] = {cccc, "--watch", "h_main.c", "h_b.c", NULL};
    Child c;
    spawn(&c, dir, argv);
    long at = EXPECT(&c, "fb=1\n", 0, "header reload: first run");

    write_file(dir, "h_b.h", "#define B 2\n");
    at = EXPECT(&c, "reloaded h_b.c: 1 changed", (size_t)at,
                "header reload: includer reloaded");
    at = EXPECT(&c, "fb=2\n", (size_t)at, "header reload: new header value");

    write_file(dir, "h_b.c",
               "#include \"h_b.h\"\n#include \"h_c.h\"\n"
               "int fb(void) { return B + C; }\n");
    at = EXPECT(&c, "fb=12\n", (size_t)at, "header reload: new include");

    write_file(dir, "h_c.h", "#define C 20\n");
    at = EXPECT(&c, "reloaded h_b.c: 1 changed", (size_t)at,
                "header reload: header added by a reload is watched");
    EXPECT(&c, "fb=22\n", (size_t)at, "header reload: new header value (2)");
    reap(&c);
}

static void test_restart(const char *dir) {
    write_file(dir, "s.c",
               "#include <stdio.h>\n#include <unistd.h>\n"
               "int main(void) { puts(\"started A\"); fflush(stdout);"
               " sleep(30); return 0; }\n");
    char *argv[] = {cccc, "--watch", "--watch-restart", "s.c", NULL};
    Child c;
    spawn(&c, dir, argv);
    long at = EXPECT(&c, "started A", 0, "restart: first run");
    write_file(dir, "s.c",
               "#include <stdio.h>\n#include <unistd.h>\n"
               "int main(void) { puts(\"started B\"); fflush(stdout);"
               " sleep(30); return 0; }\n");
    EXPECT(&c, "started B", (size_t)at, "restart: program restarted");
    reap(&c);
}

static void test_exit_waits(const char *dir) {
    write_file(
        dir, "e.c",
        "#include <stdio.h>\nint main(void) { puts(\"run 1\"); return 3; }\n");
    char *argv[] = {cccc, "--watch", "e.c", NULL};
    Child c;
    spawn(&c, dir, argv);
    long at = EXPECT(&c, "exited with 3; waiting", 0, "exit: waits for change");
    write_file(
        dir, "e.c",
        "#include <stdio.h>\nint main(void) { puts(\"run 2\"); return 0; }\n");
    EXPECT(&c, "run 2", (size_t)at, "exit: runs again after a change");
    reap(&c);
}

static void test_build(const char *dir) {
    char sub[PATH_MAX + 64];
    snprintf(sub, sizeof sub, "%s/src", dir);
    mkdir(sub, 0755);
    snprintf(sub, sizeof sub, "%s/inc", dir);
    mkdir(sub, 0755);
    write_file(dir, "inc/msg.h", "#define MSG \"one\"\n");
    write_file(dir, "src/app.c",
               "#include <stdio.h>\n#include \"msg.h\"\n"
               "int main(void) { puts(MSG); return 0; }\n");
    write_file(dir, "build.c",
               "#include <stdio.h>\n"
               "[[cccc::build]]\n"
               "int build_main(Builder *ctx) {\n"
               "    BuildTarget *app = Executable(ctx, \"app\");\n"
               "    AddSource(app, \"src/app.c\");\n"
               "    AddInclude(app, \"inc\");\n"
               "    if (BuildWatching(ctx))\n"
               "        puts(\"(watching)\");\n"
               "    return BuildDefault(ctx);\n"
               "}\n");
    // --watch alone: [[cccc::build]] in the file implies --build.
    char *argv[] = {cccc, "--watch", "--build-quiet", "build.c", NULL};
    Child c;
    spawn(&c, dir, argv);
    long at = EXPECT(&c, "(watching)", 0, "build: BuildWatching is true");
    at      = EXPECT(&c, "build succeeded", (size_t)at, "build: first build");
    at      = EXPECT(&c, "waiting for changes", (size_t)at, "build: waits");

    // A header the compiler reported triggers a rebuild.
    write_file(dir, "inc/msg.h", "#define MSG \"two\"\n");
    at = EXPECT(&c, "build succeeded", (size_t)at, "build: header edit");
    at = EXPECT(&c, "waiting for changes", (size_t)at, "build: waits again");

    // Touching the build's own output does not.
    char out[PATH_MAX + 64];
    snprintf(out, sizeof out, "%s/build/bin/app", dir);
    FILE *f = fopen(out, "a");
    if (f) {
        fputc('\n', f);
        fclose(f);
    }
    CHECK(wait_for(&c, "build succeeded", (size_t)at, 2500) < 0,
          "build: output change must not rebuild");

    // A broken script waits for the fix.
    write_file(dir, "build.c", "[[cccc::build]]\nint build_main( {\n");
    at = EXPECT(&c, "failed with", (size_t)at, "build: broken script waits");
    reap(&c);
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

static bool find_cccc(void) {
    const char *candidates[] = {getenv("CCCC_BIN"), "build/cccc", "./cccc"};
    for (size_t i = 0; i < sizeof candidates / sizeof *candidates; i++)
        if (candidates[i] && realpath(candidates[i], cccc) &&
            access(cccc, X_OK) == 0)
            return true;
    return false;
}

int main(void) {
    alarm(240);
    if (!find_cccc()) {
        printf("SKIP: watch cli (no cccc binary; set CCCC_BIN)\n");
        return 0;
    }
    char tmpl[] = "/tmp/cccc_watch_cli_XXXXXX";
    if (!mkdtemp(tmpl))
        return 2;
    realpath(tmpl, root);

    test_hot_reload(root);
    test_refused_reload(root);
    test_header_reload(root);
    test_restart(root);
    test_exit_waits(root);
    test_build(root);

    remove_tree(root);
    if (!failed)
        printf("PASS: watch cli\n");
    return failed;
}
