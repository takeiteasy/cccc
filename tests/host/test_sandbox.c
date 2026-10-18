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
#include <sys/stat.h>
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
// 16: read a file outside the allowed directories. 32: read the input's
// neighbour. 64: started the host C compiler.
static const char probe_src[] =
    "#include[[cccc::comptime]] <stdio.h>\n"
    "#include[[cccc::comptime]] <stdlib.h>\n"
    "#include[[cccc::comptime]] <sys/socket.h>\n"
    "#include[[cccc::comptime]] <netinet/in.h>\n"
    "#include[[cccc::comptime]] <spawn.h>\n"
    "#include[[cccc::comptime]] <sys/wait.h>\n"
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
    "    f = fopen(\"%s/key\", \"r\");\n"
    "    if (f) { fclose(f); r |= 16; }\n"
    "    f = fopen(\"%s/ok.c\", \"r\");\n"
    "    if (f) { fclose(f); r |= 32; }\n"
    "    pid_t pid; char *av[] = {\"cc\", \"--version\", 0}; char *ev[] = "
    "{0};\n"
    "    if (posix_spawnp(&pid, \"cc\", 0, 0, av, ev) == 0) {\n"
    "        int st = 0;\n"
    "        if (waitpid(pid, &st, 0) == pid && WIFEXITED(st) &&"
    " WEXITSTATUS(st) == 0) r |= 64;\n"
    "    }\n"
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

    // Outside the working directory and the temp directories, which are open.
    const char *home = getenv("HOME");
    char        secret[PATH_MAX], indir[PATH_MAX];
    snprintf(secret, sizeof secret, "%s/.cccc_sandbox_secret_XXXXXX",
             home ? home : "");
    snprintf(indir, sizeof indir, "%s/.cccc_sandbox_input_XXXXXX",
             home ? home : "");
    if (!home || !mkdtemp(secret) || !mkdtemp(indir)) {
        printf("SKIP: sandbox (no writable $HOME for the read probe)\n");
        run("rm -rf %s %s %s", root, secret, indir);
        return 0;
    }
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/key", secret);
    write_file(path, "secret\n");
    snprintf(path, sizeof path, "%s/ok.c", indir);
    write_file(path, "int main(void) { return 42; }\n");
    char src[sizeof probe_src + 4 * PATH_MAX];
    snprintf(src, sizeof src, probe_src, cwd, root, secret, indir);
    char probe[PATH_MAX];
    snprintf(probe, sizeof probe, "%s/probe.c", indir);
    write_file(probe, src);

    int failed = 0;
    int rc     = run("%s --sandbox %s/ok.c >/dev/null 2>&1", cccc, indir);
    if (rc != 42) {
        printf("SKIP: sandbox (not enforceable here: exit %d)\n", rc);
        goto done;
    }

    rc = run("%s --comptime-allow=all %s >/dev/null 2>&1", cccc, probe);
    if (rc != 127) {
        printf("SKIP: sandbox (unconfined probe exited %d, not 127)\n", rc);
        goto done;
    }

    snprintf(path, sizeof path, "%s/escape", cwd);
    unlink(path); // the unconfined run above wrote it

    rc = run("%s --sandbox --comptime-allow=all %s >/dev/null 2>&1", cccc,
             probe);
    if (rc != 40) {
        fprintf(stderr, "FAIL: sandboxed probe exited %d, expected 40\n", rc);
        failed = 1;
    }

    rc = run("%s --sandbox --sandbox-read=%s --comptime-allow=all %s "
             ">/dev/null 2>&1",
             cccc, secret, probe);
    if (rc != 56) {
        fprintf(stderr, "FAIL: --sandbox-read probe exited %d, expected 56\n",
                rc);
        failed = 1;
    }
    snprintf(path, sizeof path, "%s/escape", cwd);
    unlink(path);

    // --sandbox-exec lets the run start a program: sh runs `true` (macOS's
    // sh re-executes bash).
    rc = run("%s --sandbox --sandbox-exec=/bin/sh --sandbox-exec=/bin/bash "
             "--comptime-allow=all %s >/dev/null 2>&1",
             cccc, probe);
    if (rc != 41) {
        fprintf(stderr, "FAIL: --sandbox-exec probe exited %d, expected 41\n",
                rc);
        failed = 1;
    }
    snprintf(path, sizeof path, "%s/escape", cwd);
    unlink(path);

    char out[PATH_MAX];
    snprintf(out, sizeof out, "%s/ok.out", root);
    if (run("%s --sandbox -c=native -o %s %s/ok.c >/dev/null 2>&1", cccc, out,
            indir) != 0 ||
        run("%s", out) != 42) {
        fprintf(stderr, "FAIL: -c=native under --sandbox did not build\n");
        failed = 1;
    }

    // A build script and the cccc and cc it runs, all confined.
    char script[2 * PATH_MAX];
    snprintf(script, sizeof script,
             "[[cccc::build]]\n"
             "int build_main(Builder *ctx) {\n"
             "    BuildTarget *app = Executable(ctx, \"app\");\n"
             "    AddSource(app, \"%s/ok.c\");\n"
             "    return BuildDefault(ctx);\n"
             "}\n",
             indir);
    char build_c[PATH_MAX];
    snprintf(build_c, sizeof build_c, "%s/build.c", indir);
    write_file(build_c, script);
    char built[PATH_MAX];
    snprintf(built, sizeof built, "%s/bo/bin/app", root);
    if (run("%s --sandbox --build-out-dir=%s/bo --build %s >/dev/null 2>&1",
            cccc, root, build_c) != 0 ||
        run("%s", built) != 42) {
        fprintf(stderr, "FAIL: --build under --sandbox\n");
        failed = 1;
    }

    // A compiler outside the system directories reads its own tree.
    const char *other_cc = "/opt/homebrew/opt/llvm/bin/clang";
    if (access(other_cc, X_OK) == 0) {
        snprintf(built, sizeof built, "%s/bo2/bin/app", root);
        if (run("%s --sandbox --build-cc=%s --build-out-dir=%s/bo2 --build %s "
                ">/dev/null 2>&1",
                cccc, other_cc, root, build_c) != 0 ||
            run("%s", built) != 42) {
            fprintf(stderr, "FAIL: --build --build-cc=%s under --sandbox\n",
                    other_cc);
            failed = 1;
        }
        unlink(out);
        if (run("CCCC_NATIVE_CC=%s %s --sandbox -c=native -o %s %s/ok.c "
                ">/dev/null 2>&1",
                other_cc, cccc, out, indir) != 0 ||
            run("%s", out) != 42) {
            fprintf(stderr, "FAIL: -c=native with %s under --sandbox\n",
                    other_cc);
            failed = 1;
        }
    }

    // --testing=native runs the binary it builds.
    char tests_c[PATH_MAX], tests_out[PATH_MAX];
    snprintf(tests_c, sizeof tests_c, "%s/t.c", indir);
    snprintf(tests_out, sizeof tests_out, "%s/t.out", root);
    write_file(tests_c, "[[cccc::test]] void t(void) { }\n");
    if (run("%s --sandbox --testing=native -o %s %s >/dev/null 2>&1", cccc,
            tests_out, tests_c) != 0) {
        fprintf(stderr, "FAIL: --testing=native under --sandbox\n");
        failed = 1;
    }

    // --asm-passthru builds a thunk with the host cc.
    char asm_c[PATH_MAX];
    snprintf(asm_c, sizeof asm_c, "%s/asm.c", indir);
    write_file(asm_c, "int main(void) { __asm__ volatile(\"nop\"); "
                      "return 42; }\n");
    if (run("%s -A %s >/dev/null 2>&1", cccc, asm_c) == 42 &&
        run("%s --sandbox -A %s >/dev/null 2>&1", cccc, asm_c) != 42) {
        fprintf(stderr, "FAIL: --asm-passthru under --sandbox\n");
        failed = 1;
    }

#ifdef __APPLE__
    // Xcode's `as` is a shell script, which Homebrew gcc runs.
    const char *gcc = "/opt/homebrew/bin/gcc-16";
    if (access(gcc, X_OK) == 0) {
        unlink(out);
        if (run("CCCC_NATIVE_CC=%s %s --sandbox -c=native -o %s %s/ok.c "
                ">/dev/null 2>&1",
                gcc, cccc, out, indir) != 0 ||
            run("%s", out) != 42) {
            fprintf(stderr, "FAIL: -c=native with %s under --sandbox\n", gcc);
            failed = 1;
        }
    }
#endif

#ifdef __linux__
    // Only the files the loader and name lookups open are readable in /etc.
    const char *etc_files[] = {"/etc/hostname", "/etc/fstab", "/etc/hosts"};
    for (size_t i = 0; i < sizeof etc_files / sizeof *etc_files; i++) {
        struct stat st;
        if (lstat(etc_files[i], &st) != 0 || !S_ISREG(st.st_mode))
            continue;
        char etc_src[PATH_MAX + 512], etc_c[PATH_MAX];
        snprintf(
            etc_src, sizeof etc_src,
            "#include[[cccc::comptime]] <stdio.h>\n"
            "[[cccc::comptime]] int probe(void) {\n"
            "    FILE *f = fopen(\"%s\", \"r\");\n"
            "    if (f) { fclose(f); return 1; }\n"
            "    return 0;\n"
            "}\n"
            "[[cccc::comptime]] void gen(void) {\n"
            "    Obj *fn = MakeFunction(\"result\", GetType(\"int\"));\n"
            "    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(probe())));\n"
            "}\n"
            "gen();\n"
            "int main(void) { return result(); }\n",
            etc_files[i]);
        snprintf(etc_c, sizeof etc_c, "%s/etc.c", indir);
        write_file(etc_c, etc_src);
        if (run("%s --comptime-allow=all %s >/dev/null 2>&1", cccc, etc_c) !=
                1 ||
            run("%s --sandbox --comptime-allow=all %s >/dev/null 2>&1", cccc,
                etc_c) != 0) {
            fprintf(stderr, "FAIL: --sandbox read of %s, expected denied\n",
                    etc_files[i]);
            failed = 1;
        }
        break;
    }
#endif

    if (run("%s --sandbox --allow-url %s/ok.c >/dev/null 2>&1", cccc, indir) ==
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
    run("rm -rf %s %s %s", root, secret, indir);
    return failed;
}
