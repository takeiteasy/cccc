// CCCC_FLAGS: --build --build-cache=build/test_cccc_hdrdep_cache --build-out-dir=build/test_cccc_hdrdep_out
// CCCC_EXPECT_STDOUT: (?=[\s\S]*\(up to date\) cccc_hdrdep_app)(?=[\s\S]*hdrdep_ok)
//
// #1308: a CcccExecutable target now tracks the headers its sources #include
// automatically, via `cccc --compile=native --deps-file=...`, so a header
// that was never declared with AddInput() still invalidates the cached
// binary. Three Build() calls: (1) fresh compile, (2) no change -> the
// target reports "(up to date)", (3) after an *undeclared* header is
// rewritten -> the target must recompile.

#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

#ifdef __APPLE__
#define ST_MTIME(st) ((st).st_mtimespec.tv_sec)
#else
#define ST_MTIME(st) ((st).st_mtim.tv_sec)
#endif

[[cccc::build]]
int build_main(Builder *ctx) {
    WriteFile(ctx, "build/gen/hdrdep.h", "#define HDRDEP_VAL 42\n");
    WriteFile(ctx, "build/gen/hdrdep_main.c",
              "#include \"hdrdep.h\"\n"
              "int main(void) { return HDRDEP_VAL - 42; }\n");

    BuildTarget *app = CcccExecutable(ctx, "cccc_hdrdep_app");
    AddSource(app, "build/gen/hdrdep_main.c");
    AddInclude(app, "build/gen");
    // Deliberately NO AddInput(app, "build/gen/hdrdep.h").

    if (Build(ctx, app) != 0)
        return 1;
    const char *binpath = TargetOutput(app);
    struct stat st1;
    if (stat(binpath, &st1) != 0)
        return 1;

    if (Build(ctx, app) != 0)
        return 1;
    struct stat st2;
    if (stat(binpath, &st2) != 0)
        return 1;
    if (ST_MTIME(st2) != ST_MTIME(st1)) {
        printf("FAIL: cccc target rebuilt when nothing changed\n");
        return 1;
    }

    // Advance the clock past the 1s mtime resolution the staleness check
    // uses, then change a header the target never declared.
    sleep(1);
    WriteFile(ctx, "build/gen/hdrdep.h", "#define HDRDEP_VAL 42 /* touched */\n");

    if (Build(ctx, app) != 0)
        return 1;
    struct stat st3;
    if (stat(binpath, &st3) != 0)
        return 1;
    if (ST_MTIME(st3) == ST_MTIME(st1)) {
        printf("FAIL: cccc target not rebuilt after an undeclared header "
               "changed\n");
        return 1;
    }

    printf("hdrdep_ok\n");
    return 0;
}
