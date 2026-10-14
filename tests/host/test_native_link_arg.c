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

// End-to-end test of `-c=native --link-arg`: a helper object compiled by the
// host cc is linked into a program through --link-arg. The
// binary is $CCCC_BIN, else build/cccc, else ./cccc; the test is skipped when
// none exists.

#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static char cccc[PATH_MAX];

static bool find_cccc(void) {
    const char *candidates[] = {getenv("CCCC_BIN"), "build/cccc", "./cccc"};
    for (size_t i = 0; i < sizeof candidates / sizeof *candidates; i++)
        if (candidates[i] && realpath(candidates[i], cccc) &&
            access(cccc, X_OK) == 0)
            return true;
    return false;
}

static int run(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static int run(const char *fmt, ...) {
    char    cmd[4 * PATH_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cmd, sizeof cmd, fmt, ap);
    va_end(ap);
    int st = system(cmd);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

int main(void) {
    alarm(120);
    if (!find_cccc()) {
        printf("SKIP: native link-arg (no cccc binary; set CCCC_BIN)\n");
        return 0;
    }
    char root[] = "/tmp/cccc_link_arg_XXXXXX";
    if (!mkdtemp(root))
        return 2;

    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/helper.c", root);
    write_file(path, "int helper(void) { return 40; }\n"
                     "int helper2(void) { return 2; }\n");
    snprintf(path, sizeof path, "%s/main.c", root);
    write_file(path, "int helper(void);\nint helper2(void);\n"
                     "int main(void) { return helper() + helper2(); }\n");

    int failed = 0;
    if (run("cc -c %s/helper.c -o %s/helper.o", root, root) != 0) {
        fprintf(stderr, "FAIL: host cc could not build helper.o\n");
        failed = 1;
    } else if (run("%s -c=native %s/main.c --link-arg=%s/helper.o -o %s/app "
                   ">/dev/null 2>&1",
                   cccc, root, root, root) != 0) {
        fprintf(stderr, "FAIL: -c=native with --link-arg=helper.o failed\n");
        failed = 1;
    } else if (run("%s/app", root) != 42) {
        fprintf(stderr, "FAIL: linked program did not return 42\n");
        failed = 1;
    }

    // Without the object the link fails, so the success above came from it.
    if (!failed && run("%s -c=native %s/main.c -o %s/app2 >/dev/null 2>&1",
                       cccc, root, root) == 0) {
        fprintf(stderr, "FAIL: link succeeded without helper.o\n");
        failed = 1;
    }

    const char *files[] = {"helper.c", "helper.o", "main.c", "app", "app2"};
    for (size_t i = 0; i < sizeof files / sizeof *files; i++) {
        snprintf(path, sizeof path, "%s/%s", root, files[i]);
        unlink(path);
    }
    rmdir(root);
    if (!failed)
        printf("PASS: native link-arg\n");
    return failed;
}
