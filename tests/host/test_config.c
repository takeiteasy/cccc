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

// Config files: every command-line option has a class in src/config.c, and
// the real binary reads global/project configs, applies the trust rules and
// honours --no-config. The binary is $CCCC_BIN, else build/cccc, else ./cccc;
// the binary half is skipped when none exists.

#include "config.h"

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
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

// --- class table ---------------------------------------------------------

static void test_every_option_has_a_class(void) {
    for (int i = 0; cccc_long_options[i].name; i++) {
        const char *name = cccc_long_options[i].name;
        int         cls  = cccc_config_class(name);
        CHECK(cls >= 0, "--%s has no class in src/config.c", name);
        int val = cccc_long_options[i].val;
        if (val > 0 && val < 256) {
            char letter[2] = {(char)val, 0};
            CHECK(cccc_config_class(letter) == cls,
                  "--%s and -%c have different classes", name, val);
        }
    }
    for (const char *p = cccc_optstring; *p; p++) {
        if (*p == ':')
            continue;
        char letter[2] = {*p, 0};
        CHECK(cccc_config_class(letter) >= 0, "-%c has no class", *p);
    }
}

static bool option_exists(const char *key) {
    if (key[0] && !key[1])
        return strchr(cccc_optstring, key[0]) != NULL;
    for (int i = 0; cccc_long_options[i].name; i++)
        if (!strcmp(cccc_long_options[i].name, key))
            return true;
    return false;
}

static void test_class_spot_checks(void) {
    // Keys the docs name must exist and keep their class.
    const char *must[] = {"comptime-allow", "library", "out",  "std", "D",
                          "build",          "repl",    "trust"};
    for (size_t i = 0; i < sizeof must / sizeof *must; i++)
        CHECK(option_exists(must[i]), "%s is not an option", must[i]);
    CHECK(cccc_config_class("comptime-allow") == CFG_TRUST, "comptime-allow");
    CHECK(cccc_config_class("library") == CFG_TRUST, "library");
    CHECK(cccc_config_class("std") == CFG_FREE, "std");
    CHECK(cccc_config_class("repl") == CFG_CLI_ONLY, "repl");
    CHECK(cccc_config_class("debug") == CFG_CLI_ONLY, "debug opens a prompt");
    CHECK(cccc_config_class("ffi-allow") == CFG_TRUST, "ffi-allow can widen");
    CHECK(cccc_config_class("asm-passthru") == CFG_TRUST, "asm-passthru");
    CHECK(cccc_config_class("no-such-option") == -1, "unknown key");
}

// --- running the binary --------------------------------------------------

typedef struct {
    int  rc;
    char out[1 << 14]; // stdout and stderr
} Run;

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

static void make_dir(const char *dir, const char *name) {
    char p[PATH_MAX + 64];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    mkdir(p, 0755);
}

// Runs cccc in `dir` with HOME and XDG_CONFIG_HOME inside `root`, so no real
// user config is read and prompting is off.
static Run run(const char *dir, char *const argv[]) {
    Run r = {0};
    int fd[2];
    if (pipe(fd) != 0)
        exit(2);
    pid_t pid = fork();
    if (pid == 0) {
        char home[PATH_MAX + 16], xdg[PATH_MAX + 32];
        snprintf(home, sizeof home, "%s/home", root);
        snprintf(xdg, sizeof xdg, "%s/home/xdg", root);
        setenv("HOME", home, 1);
        setenv("XDG_CONFIG_HOME", xdg, 1);
        setenv("CCCC_CONFIG_NO_PROMPT", "1", 1);
        if (getenv("CCCC_TEST_KEEP_NO_CONFIG") == NULL)
            unsetenv("CCCC_NO_CONFIG");
        if (chdir(dir) != 0)
            _exit(127);
        dup2(fd[1], STDOUT_FILENO);
        dup2(fd[1], STDERR_FILENO);
        close(fd[0]);
        close(fd[1]);
        execv(cccc, argv);
        _exit(127);
    }
    close(fd[1]);
    size_t  len = 0;
    ssize_t n;
    while ((n = read(fd[0], r.out + len, sizeof r.out - 1 - len)) > 0)
        len += (size_t)n;
    close(fd[0]);
    int st;
    waitpid(pid, &st, 0);
    r.rc = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    return r;
}

#define RUN(dir, ...) run(dir, (char *const[]){cccc, __VA_ARGS__, NULL})

static const char ANSWER_C[] = "int main(void) {\n"
                               "#ifdef ANSWER\n"
                               "    return ANSWER;\n"
                               "#else\n"
                               "    return 1;\n"
                               "#endif\n"
                               "}\n";

// Reads /dev/null from comptime, which needs --comptime-allow=fs.
static const char FS_C[] =
    "#include[[cccc::comptime]] <stdio.h>\n"
    "[[cccc::comptime]]\n"
    "int probe(void) {\n"
    "    FILE *f = fopen(\"/dev/null\", \"r\");\n"
    "    if (f)\n"
    "        fclose(f);\n"
    "    return 42;\n"
    "}\n"
    "[[cccc::comptime]]\n"
    "void gen(void) {\n"
    "    Obj *fn = MakeFunction(\"result\", GetType(\"int\"));\n"
    "    FunctionSetBody(fn, MakeReturn(MakeIntLiteral(probe())));\n"
    "}\n"
    "gen();\n"
    "int main(void) { return result(); }\n";

static void reset(char *proj) {
    char cmd[PATH_MAX * 2];
    snprintf(cmd, sizeof cmd, "rm -rf '%s/home' '%s/proj'", root, root);
    if (system(cmd) != 0)
        exit(2);
    make_dir(root, "home");
    make_dir(root, "home/xdg");
    make_dir(root, "home/xdg/cccc");
    make_dir(root, "proj");
    make_dir(root, "proj/sub");
    snprintf(proj, PATH_MAX, "%s/proj", root);
    write_file(proj, "answer.c", ANSWER_C);
    write_file(proj, "fs.c", FS_C);
}

static void test_free_keys(const char *proj) {
    write_file(proj, ".cccc.toml", "# comment\nD = \"ANSWER=42\"\n");
    Run r = RUN(proj, "answer.c");
    CHECK(r.rc == 42, "project D key: rc=%d %s", r.rc, r.out);

    char sub[PATH_MAX + 8];
    snprintf(sub, sizeof sub, "%s/sub", proj);
    r = RUN(sub, "../answer.c");
    CHECK(r.rc == 42, "found from a subdirectory: rc=%d %s", r.rc, r.out);

    r = RUN(proj, "-DANSWER=7", "answer.c");
    CHECK(r.rc == 7, "command line beats config: rc=%d", r.rc);

    r = RUN(proj, "--no-config", "answer.c");
    CHECK(r.rc == 1, "--no-config: rc=%d", r.rc);

    setenv("CCCC_TEST_KEEP_NO_CONFIG", "1", 1);
    setenv("CCCC_NO_CONFIG", "1", 1);
    r = RUN(proj, "answer.c");
    CHECK(r.rc == 1, "CCCC_NO_CONFIG: rc=%d", r.rc);
    unsetenv("CCCC_NO_CONFIG");
    unsetenv("CCCC_TEST_KEEP_NO_CONFIG");
}

static void test_values(const char *proj) {
    write_file(
        proj, ".cccc.toml",
        "D = [\"A=1\", \"F(x,y)=2\"]\nW = [\"all\", \"error=format\"]\n");
    Run r = RUN(proj, "--show-config", "answer.c");
    CHECK(strstr(r.out, "-DA=1 -DF(x,y)=2 -Wall -Werror=format"),
          "arrays give one option per item: %s", r.out);

    write_file(proj, ".cccc.toml", "U = \"X\"\nD = \"X\"\n");
    r = RUN(proj, "--show-config", "answer.c");
    CHECK(strstr(r.out, "-UX -DX"), "a list key takes one string: %s", r.out);

    write_file(proj, ".cccc.toml", "U = [\"X\"]\nD = [\"X\"]\n");
    r = RUN(proj, "--show-config", "answer.c");
    CHECK(strstr(r.out, "-UX -DX"), "keys keep file order: %s", r.out);

    write_file(proj, ".cccc.toml", "max-errors = 5\nO = 2\n");
    r = RUN(proj, "--show-config", "answer.c");
    CHECK(strstr(r.out, "--max-errors=5 -O2"), "integers: %s", r.out);

    write_file(proj, ".cccc.toml", "openmp = true\n");
    r = RUN(proj, "--show-config", "answer.c");
    CHECK(strstr(r.out, "options: --openmp"), "a true flag: %s", r.out);

    write_file(proj, ".cccc.toml", "openmp = false\n");
    r = RUN(proj, "--show-config", "answer.c");
    CHECK(!strstr(r.out, "--openmp"), "a false flag adds nothing: %s", r.out);

    char g[PATH_MAX + 32];
    snprintf(g, sizeof g, "%s/home/xdg/cccc", root);
    write_file(g, "config.toml", "link-arg = [\"-Wl,-rpath,x\", \"-lm\"]\n");
    r = RUN(proj, "--show-config", "answer.c");
    CHECK(strstr(r.out, "--link-arg=-Wl,-rpath,x --link-arg=-lm"),
          "a value with commas passes through: %s", r.out);
}

static void test_global_and_local_order(const char *proj) {
    char g[PATH_MAX + 32];
    snprintf(g, sizeof g, "%s/home/xdg/cccc", root);
    write_file(g, "config.toml", "D = \"ANSWER=1\"\n");
    write_file(proj, ".cccc.toml", "D = \"ANSWER=42\"\n");
    Run r = RUN(proj, "answer.c");
    CHECK(r.rc == 42, "local beats global: rc=%d %s", r.rc, r.out);
    write_file(proj, ".cccc.toml", "# nothing\n");
    r = RUN(proj, "answer.c");
    CHECK(r.rc == 1, "global applies: rc=%d", r.rc);
    snprintf(g, sizeof g, "%s/home/xdg/cccc/config.toml", root);
    unlink(g);
}

// Writes `text` as the project config and expects exit 1 with `pattern`.
static void expect_error(const char *proj, const char *text,
                         const char *pattern) {
    write_file(proj, ".cccc.toml", text);
    Run r = RUN(proj, "answer.c");
    CHECK(r.rc == 1 && strstr(r.out, pattern), "%s: want \"%s\", rc=%d %s",
          text, pattern, r.rc, r.out);
}

static void test_bad_input(const char *proj) {
    expect_error(proj, "std = \"c17\"\nrepl = true\n",
                 ".cccc.toml:2: 'repl' cannot be set");
    expect_error(proj, "bogus = 1\n", ".cccc.toml:1: unknown key 'bogus'");
    expect_error(proj, "[section]\n", ".cccc.toml:1: tables are not supported");
    expect_error(proj, "D.x = \"1\"\n",
                 ".cccc.toml:1: tables are not supported");
    expect_error(proj, "openmp = \"maybe\"\n", "is a flag");
    expect_error(proj, "openmp = 1\n", "is a flag");
    expect_error(proj, "std = true\n", ".cccc.toml:1: 'std' needs a value");
    expect_error(proj, "std = \"\"\n", ".cccc.toml:1: 'std' needs a value");
    expect_error(proj, "std = [\"c17\"]\n", "takes one value, not a list");
    expect_error(proj, "D = 1.5\n", "'D' cannot be of type float");
    expect_error(proj, "D = 1979-05-27\n", "cannot be of type date or time");
    expect_error(proj, "D = [[\"a\"]]\n", "list items must be strings");
    expect_error(proj, "D = [true]\n", "list items must be strings");
    expect_error(proj, "std = \"c17\"\nstd = \"c11\"\n",
                 ".cccc.toml:2: duplicate key");
    expect_error(proj, "std = \"c17\"\nD = [\n", ".cccc.toml:3:");
    expect_error(proj, "std = c17\n", ".cccc.toml:1:");
}

static void test_relative_paths(const char *proj) {
    make_dir(proj, "inc");
    char inc[PATH_MAX + 8];
    snprintf(inc, sizeof inc, "%s/inc", proj);
    write_file(inc, "x.h", "#define VAL 42\n");
    write_file(proj, "rel.c",
               "#include \"x.h\"\nint main(void) { return VAL; }\n");
    write_file(proj, ".cccc.toml", "I = \"inc\"\n");
    char sub[PATH_MAX + 8];
    snprintf(sub, sizeof sub, "%s/sub", proj);
    Run r = RUN(sub, "../rel.c");
    CHECK(r.rc == 42, "relative I resolves against the config dir: rc=%d %s",
          r.rc, r.out);
    r = RUN(sub, "--show-config", "../rel.c");
    CHECK(strstr(r.out, "-I/") && strstr(r.out, "/proj/inc"),
          "show-config prints the resolved path: %s", r.out);
}

// Escapes decode to control characters after parsing, so the check runs on
// decoded text and no raw escape byte reaches the terminal.
static void test_control_characters(const char *proj) {
    const char *bad[] = {
        "D = \"A\\u001b[2K=1\"\n",         // ESC in a value
        "D = [\"ok\", \"A\\u001b[2K\"]\n", // ESC in an array item
        "\"D\\u001b[2K\" = \"x\"\n",       // ESC in a key
        "D = \"A\\u009b2K\"\n",            // C1 control (CSI)
        "D = \"A\\u0000B\"\n",             // NUL
        "D = \"A\\u007fB\"\n",             // DEL
        "D = 'A\x1b[2K'\n",                // raw ESC in a literal string
    };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        write_file(proj, ".cccc.toml", bad[i]);
        Run r = RUN(proj, "answer.c");
        CHECK(r.rc == 1 && !strchr(r.out, 0x1b) && !strchr(r.out, 0x7f) &&
                  !strstr(r.out, "\xc2\x9b"),
              "control character %zu: rc=%d %s", i, r.rc, r.out);
        if (i < 6)
            CHECK(strstr(r.out, ".cccc.toml:1: control characters"),
                  "control character %zu is named: %s", i, r.out);
    }
}

static void test_build_ignores_config(const char *proj) {
    write_file(proj, ".cccc.toml", "D = \"ANSWER=42\"\n");
    Run r = RUN(proj, "--build", "--show-config", "answer.c");
    CHECK(strstr(r.out, "config disabled"), "--build skips config: %s", r.out);
}

static void test_symlinked_global_config(const char *proj) {
    char real[PATH_MAX + 32], link[PATH_MAX + 48];
    snprintf(real, sizeof real, "%s/home", root);
    write_file(real, "dotfile", "D = \"ANSWER=42\"\n");
    snprintf(real, sizeof real, "%s/home/dotfile", root);
    snprintf(link, sizeof link, "%s/home/xdg/cccc/config.toml", root);
    if (symlink(real, link) != 0)
        exit(2);
    Run r = RUN(proj, "answer.c");
    CHECK(r.rc == 42, "a symlinked user config is read: rc=%d %s", r.rc, r.out);
}

static void test_symlinked_config_ignored(const char *proj) {
    write_file(proj, "real.cfg", "D = \"ANSWER=42\"\n");
    char link[PATH_MAX + 16], target[PATH_MAX + 16];
    snprintf(link, sizeof link, "%s/.cccc.toml", proj);
    snprintf(target, sizeof target, "%s/real.cfg", proj);
    unlink(link);
    if (symlink(target, link) != 0)
        exit(2);
    Run r = RUN(proj, "answer.c");
    CHECK(r.rc == 1 && strstr(r.out, "not a regular file owned by you"),
          "a symlinked .cccc.toml is ignored with a warning: rc=%d %s", r.rc,
          r.out);
    unlink(link);
}

static void test_trust(const char *proj) {
    write_file(proj, ".cccc.toml", "comptime-allow = [\"fs\"]\n");
    Run r = RUN(proj, "fs.c");
    CHECK(r.rc != 42 &&
              strstr(r.out, "ignoring comptime-allow=fs (not trusted)"),
          "untrusted grant is ignored with a warning: rc=%d %s", r.rc, r.out);
    CHECK(strstr(r.out, "denied (class fs)"), "and the call is denied: %s",
          r.out);

    r = RUN(proj, "--show-config", "fs.c");
    CHECK(strstr(r.out, "untrusted  comptime-allow=fs"), "show-config: %s",
          r.out);

    r = RUN(proj, "--trust");
    CHECK(r.rc == 0 && strstr(r.out, "comptime-allow=fs"), "--trust: %s",
          r.out);
    r = RUN(proj, "fs.c");
    CHECK(r.rc == 42, "trusted grant applies: rc=%d %s", r.rc, r.out);

    write_file(proj, ".cccc.toml", "comptime-allow = [\"fs\", \"exec\"]\n");
    r = RUN(proj, "fs.c");
    CHECK(r.rc == 42 && strstr(r.out, "ignoring comptime-allow=exec") &&
              !strstr(r.out, "ignoring comptime-allow=fs "),
          "only the new grant is withheld: rc=%d %s", r.rc, r.out);

    r = RUN(proj, "--untrust");
    CHECK(r.rc == 0, "--untrust: %s", r.out);
    write_file(proj, ".cccc.toml", "comptime-allow = \"fs\"\n");
    r = RUN(proj, "fs.c");
    CHECK(r.rc != 42, "grant is untrusted again after --untrust: rc=%d", r.rc);
}

static char *read_trusted(void) {
    static char buf[1 << 14];
    char        p[PATH_MAX + 32];
    snprintf(p, sizeof p, "%s/home/xdg/cccc/trusted", root);
    FILE  *f = fopen(p, "r");
    size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
    if (f)
        fclose(f);
    buf[n] = '\0';
    return buf;
}

static int count_of(const char *hay, const char *needle) {
    int n = 0;
    for (const char *p = hay; (p = strstr(p, needle)); p += strlen(needle))
        n++;
    return n;
}

static void test_trust_hash(const char *proj) {
    write_file(proj, ".cccc.toml", "comptime-allow = [\"fs\"]\n");
    Run r = RUN(proj, "--trust", "--trust-hash");
    CHECK(r.rc == 0 && strstr(r.out, "pinned by sha256"), "--trust-hash: %s",
          r.out);
    r = RUN(proj, "fs.c");
    CHECK(r.rc == 42, "pinned file applies: rc=%d %s", r.rc, r.out);
    r = RUN(proj, "--show-config", "fs.c");
    CHECK(strstr(r.out, "pinned    (sha256 matches)"), "show pinned: %s",
          r.out);

    // An edit that adds no grant is still noticed.
    write_file(proj, ".cccc.toml", "comptime-allow = [\"fs\"]\n# edited\n");
    r = RUN(proj, "fs.c");
    CHECK(
        r.rc != 42 &&
            strstr(r.out, "ignoring comptime-allow=fs (changed since trusted)"),
        "edit withholds the grant: rc=%d %s", r.rc, r.out);
    r = RUN(proj, "--show-config", "fs.c");
    CHECK(strstr(r.out, "untrusted  comptime-allow=fs") &&
              strstr(r.out, "changed since trusted (sha256 pin differs)"),
          "show changed: %s", r.out);

    // A plain --trust keeps the pin and refreshes the hash.
    r = RUN(proj, "--trust");
    CHECK(r.rc == 0 && strstr(r.out, "pinned by sha256"), "repin: %s", r.out);
    r = RUN(proj, "fs.c");
    CHECK(r.rc == 42, "repinned file applies: rc=%d %s", r.rc, r.out);
    const char *list = read_trusted();
    CHECK(count_of(list, "@sha256=") == 1, "one pin line: %s", list);
    CHECK(count_of(list, "comptime-allow=fs") == 1, "grant not duplicated: %s",
          list);

    r = RUN(proj, "--trust", "--trust-hash=off");
    CHECK(r.rc == 0 && strstr(r.out, "no longer pinned"), "unpin: %s", r.out);
    write_file(proj, ".cccc.toml", "comptime-allow = [\"fs\"]\n# again\n");
    r = RUN(proj, "fs.c");
    CHECK(r.rc == 42, "unpinned file ignores edits: rc=%d %s", r.rc, r.out);

    r = RUN(proj, "--trust", "--trust-hash=maybe");
    CHECK(r.rc != 0 && strstr(r.out, "takes on or off"), "bad value: %s",
          r.out);

    RUN(proj, "--trust", "--trust-hash");
    r = RUN(proj, "--untrust");
    CHECK(r.rc == 0 && !strstr(read_trusted(), "@sha256="),
          "--untrust clears the pin: %s", read_trusted());
}

static void test_trust_hash_user_default(const char *proj) {
    char g[PATH_MAX + 32];
    snprintf(g, sizeof g, "%s/home/xdg/cccc", root);
    write_file(g, "config.toml", "trust-hash = true\n");
    write_file(proj, ".cccc.toml", "comptime-allow = [\"fs\"]\n");
    Run r = RUN(proj, "--trust");
    CHECK(r.rc == 0 && strstr(r.out, "pinned by sha256"),
          "user default pins: %s", r.out);
    write_file(proj, ".cccc.toml", "comptime-allow = [\"fs\"]\n# edited\n");
    r = RUN(proj, "fs.c");
    CHECK(r.rc != 42 && strstr(r.out, "changed since trusted"),
          "user default catches edits: rc=%d %s", r.rc, r.out);
    r = RUN(proj, "--trust", "--trust-hash=off");
    CHECK(r.rc == 0 && strstr(r.out, "no longer pinned"),
          "command line overrides the default: %s", r.out);
    CHECK(!strstr(read_trusted(), "@sha256="), "no pin left: %s",
          read_trusted());
}

static void test_trust_hash_project_rejected(const char *proj) {
    write_file(proj, ".cccc.toml", "trust-hash = true\n");
    Run r = RUN(proj, "answer.c");
    CHECK(r.rc != 0 && strstr(r.out, "can only be set in the user config"),
          "project trust-hash is an error: rc=%d %s", r.rc, r.out);
}

static void test_global_grants_without_trust(const char *proj) {
    char g[PATH_MAX + 32];
    snprintf(g, sizeof g, "%s/home/xdg/cccc", root);
    write_file(g, "config.toml", "comptime-allow = \"fs\"\n");
    char p[PATH_MAX + 16];
    snprintf(p, sizeof p, "%s/.cccc.toml", proj);
    unlink(p);
    Run r = RUN(proj, "fs.c");
    CHECK(r.rc == 42, "global config is trusted: rc=%d %s", r.rc, r.out);
}

static bool find_binary(void) {
    const char *env   = getenv("CCCC_BIN");
    const char *try[] = {env, "build/cccc", "./cccc"};
    for (int i = 0; i < 3; i++)
        if (try[i] && access(try[i], X_OK) == 0 && realpath(try[i], cccc))
            return true;
    return false;
}

int main(void) {
    test_every_option_has_a_class();
    test_class_spot_checks();

    if (!find_binary()) {
        fprintf(stderr,
                "test_config: no cccc binary; skipping the run tests\n");
        return failed ? 1 : 0;
    }
    const char *tmp = getenv("TMPDIR");
    snprintf(root, sizeof root, "%s/cccc-config-XXXXXX",
             tmp && tmp[0] ? tmp : "/tmp");
    if (!mkdtemp(root))
        return 2;
    char real[PATH_MAX];
    if (realpath(root, real))
        snprintf(root, sizeof root, "%s", real);

    char proj[PATH_MAX];
    reset(proj);
    test_free_keys(proj);
    reset(proj);
    test_values(proj);
    reset(proj);
    test_global_and_local_order(proj);
    reset(proj);
    test_bad_input(proj);
    reset(proj);
    test_relative_paths(proj);
    reset(proj);
    test_control_characters(proj);
    reset(proj);
    test_build_ignores_config(proj);
    reset(proj);
    test_symlinked_global_config(proj);
    reset(proj);
    test_symlinked_config_ignored(proj);
    reset(proj);
    test_trust(proj);
    reset(proj);
    test_trust_hash(proj);
    reset(proj);
    test_trust_hash_user_default(proj);
    reset(proj);
    test_trust_hash_project_rejected(proj);
    reset(proj);
    test_global_grants_without_trust(proj);

    char cmd[PATH_MAX + 16];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    if (system(cmd) != 0)
        fprintf(stderr, "test_config: could not remove %s\n", root);
    if (!failed)
        printf("PASS: config\n");
    return failed ? 1 : 0;
}
