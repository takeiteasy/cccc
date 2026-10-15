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

// Self-hosting check for src/watch.c (and its vendored dmon.h): compiles it
// with `-c=native` next to a tiny driver, links CoreServices and runs the
// result. macOS only; skipped without a cccc binary ($CCCC_BIN, else
// build/cccc, else ./cccc) or an SDK.

#include <ftw.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__

// Watches argv[1], rewrites it until FSEvents reports it (events right after
// arm can be missed) and exits 42 once the change is seen.
static const char DRIVER[] =
    "#include \"watch.h\"\n"
    "#include <stdio.h>\n"
    "#include <stdlib.h>\n"
    "#include <string.h>\n"
    "#include <unistd.h>\n"
    "static void noop(void) {}\n"
    "int main(int argc, char **argv) {\n"
    "    if (argc < 2)\n"
    "        return 1;\n"
    "    CcWatchOptions opt = {.debounce_ms = 50, .argv = argv,\n"
    "                          .on_change = noop};\n"
    "    if (!cc_watch_init(&opt))\n"
    "        return 2;\n"
    "    cc_watch_add(argv[1]);\n"
    "    if (!cc_watch_arm())\n"
    "        return 3;\n"
    "    if (!cc_watch_active() || cc_watch_file_count() != 1)\n"
    "        return 4;\n"
    "    for (int i = 0; i < 100; i++) {\n"
    "        FILE *f = fopen(argv[1], \"a\");\n"
    "        if (f) {\n"
    "            fputs(\"x\\n\", f);\n"
    "            fclose(f);\n"
    "        }\n"
    "        usleep(100000);\n"
    "        int    n = 0;\n"
    "        char **v = cc_watch_take(&n);\n"
    "        for (int j = 0; j < n; j++)\n"
    "            free(v[j]);\n"
    "        free(v);\n"
    "        if (n > 0)\n"
    "            return 42;\n"
    "    }\n"
    "    return 5;\n"
    "}\n";

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

static int remove_entry(const char *path, const struct stat *st, int type,
                        struct FTW *ftw) {
    (void)st;
    (void)type;
    (void)ftw;
    return remove(path);
}

static int find_sdk(char *sdk, size_t n) {
    FILE *p = popen("xcrun --show-sdk-path 2>/dev/null", "r");
    if (!p)
        return 0;
    int ok = fgets(sdk, (int)n, p) != NULL;
    pclose(p);
    sdk[strcspn(sdk, "\n")] = '\0';
    return ok && sdk[0] && access(sdk, R_OK) == 0;
}

static int find_cccc(char *out) {
    const char *candidates[] = {getenv("CCCC_BIN"), "build/cccc", "./cccc"};
    for (size_t i = 0; i < sizeof candidates / sizeof *candidates; i++)
        if (candidates[i] && realpath(candidates[i], out) &&
            access(out, X_OK) == 0)
            return 1;
    return 0;
}

int main(void) {
    alarm(240);
    char cccc[PATH_MAX], sdk[PATH_MAX];
    if (!find_cccc(cccc) || !find_sdk(sdk, sizeof sdk)) {
        printf("SKIP: watch selfhost (needs a cccc binary and a macOS SDK)\n");
        return 0;
    }
    char root[] = "/tmp/cccc_watch_selfhost_XXXXXX";
    if (!mkdtemp(root))
        return 2;

    char driver[PATH_MAX], exe[PATH_MAX];
    snprintf(driver, sizeof driver, "%s/main.c", root);
    snprintf(exe, sizeof exe, "%s/watch_selfhost", root);
    FILE *f = fopen(driver, "w");
    if (!f)
        return 2;
    fputs(DRIVER, f);
    fclose(f);

    int rc     = run("'%s' --use-system-headers -i '%s/usr/include' "
                     "--iframework '%s/System/Library/Frameworks' -I include "
                     "-I src -c=native src/watch.c '%s' --link-arg=-framework "
                     "--link-arg=CoreServices -o '%s' >%s/build.log 2>&1",
                     cccc, sdk, sdk, driver, exe, root);
    int failed = 0;
    if (rc != 0) {
        fprintf(stderr, "FAIL: -c=native build of src/watch.c exited %d\n", rc);
        run("cat '%s/build.log' >&2", root);
        failed = 1;
    } else if ((rc = run("'%s' '%s'", exe, driver)) != 42) {
        fprintf(stderr, "FAIL: self-hosted watcher exited %d, want 42\n", rc);
        failed = 1;
    }
    nftw(root, remove_entry, 8, FTW_DEPTH | FTW_PHYS);
    if (!failed)
        printf("PASS: watch selfhost\n");
    return failed;
}

#else

int main(void) {
    printf("SKIP: watch selfhost (macOS only)\n");
    return 0;
}

#endif
