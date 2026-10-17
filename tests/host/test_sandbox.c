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

// --sandbox: comptime code that tries to run a command, write outside the
// output directories and bind a socket is stopped by the OS, while a write to
// the temp directory and a -c=native build still work. The binary is
// $CCCC_BIN, else build/cccc, else ./cccc. Skipped when none exists or the
// kernel cannot enforce the sandbox (Linux without landlock).

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

// Bit 1: ran a command. 2: wrote outside the allowed directories.
// 4: bound a socket. 8: wrote into the temp directory.
static const char probe_src[] =
    "#include[[cccc::comptime]] <stdio.h>\n"
    "#include[[cccc::comptime]] <stdlib.h>\n"
    "#include[[cccc::comptime]] <sys/socket.h>\n"
    "#include[[cccc::comptime]] <netinet/in.h>\n"
    "[[cccc::comptime]] int probe(void) {\n"
    "    int r = 0;\n"
    "    if (system(\"true\") == 0) r |= 1;\n"
    "    FILE *f = fopen(\"%s/escape\", \"w\");\n"
    "    if (f) { fclose(f); r |= 2; }\n"
    "    int fd = socket(AF_INET, SOCK_STREAM, 0);\n"
    "    struct sockaddr_in a = {0};\n"
    "    a.sin_family = AF_INET;\n"
    "    a.sin_addr.s_addr = 0x0100007f;\n"
    "    if (fd >= 0 && bind(fd, (struct sockaddr *)&a, sizeof a) == 0)"
    " r |= 4;\n"
    "    f = fopen(\"%s/inside\", \"w\");\n"
    "    if (f) { fclose(f); r |= 8; }\n"
    "    return r;\n"
    "}\n"
    "[[cccc::comptime]] void gen(void) {\n"
    "    Obj *fn = MakeFunction(\"result\", GetType(\"int\"));\n"
    "    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(probe())));\n"
    "}\n"
    "gen();\n"
    "int main(void) { return result(); }\n";

int main(void) {
    alarm(120);
    if (!find_cccc()) {
        printf("SKIP: sandbox (no cccc binary; set CCCC_BIN)\n");
        return 0;
    }
    char root[] = "/tmp/cccc_sandbox_XXXXXX";
    if (!mkdtemp(root))
        return 2;
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof cwd))
        return 2;

    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/ok.c", root);
    write_file(path, "int main(void) { return 42; }\n");
    char src[sizeof probe_src + 2 * PATH_MAX];
    snprintf(src, sizeof src, probe_src, cwd, root);
    char probe[PATH_MAX];
    snprintf(probe, sizeof probe, "%s/probe.c", root);
    write_file(probe, src);

    int failed = 0;
    int rc     = run("%s --sandbox %s/ok.c >/dev/null 2>&1", cccc, root);
    if (rc != 42) {
        printf("SKIP: sandbox (not enforceable here: exit %d)\n", rc);
        goto done;
    }

    rc = run("%s --comptime-allow=all %s >/dev/null 2>&1", cccc, probe);
    if (rc != 15) {
        printf("SKIP: sandbox (unconfined probe exited %d, not 15)\n", rc);
        goto done;
    }

    snprintf(path, sizeof path, "%s/escape", cwd);
    unlink(path); // the unconfined run above wrote it

    rc = run("%s --sandbox --comptime-allow=all %s >/dev/null 2>&1", cccc,
             probe);
    if (rc != 8) {
        fprintf(stderr, "FAIL: sandboxed probe exited %d, expected 8\n", rc);
        failed = 1;
    }

    char out[PATH_MAX];
    snprintf(out, sizeof out, "%s/ok.out", root);
    if (run("%s --sandbox -c=native -o %s %s/ok.c >/dev/null 2>&1", cccc, out,
            root) != 0 ||
        run("%s", out) != 42) {
        fprintf(stderr, "FAIL: -c=native under --sandbox did not build\n");
        failed = 1;
    }

    if (run("%s --sandbox --allow-url %s/ok.c >/dev/null 2>&1", cccc, root) ==
        42) {
        fprintf(stderr, "FAIL: --sandbox accepted --allow-url\n");
        failed = 1;
    }

    if (access(path, F_OK) == 0) {
        fprintf(stderr, "FAIL: sandboxed comptime created %s\n", path);
        unlink(path);
        failed = 1;
    }
    if (!failed)
        printf("PASS: sandbox\n");

done:
    run("rm -rf %s", root);
    return failed;
}
