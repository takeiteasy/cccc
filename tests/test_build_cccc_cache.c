// CCCC_FLAGS: --build --build-cache=build/test_cccc_cache_cache --build-out-dir=build/test_cccc_cache_out
// CCCC_EXPECT_STDOUT: (?=[\s\S]*\(up to date\) cccc_cache_app)(?=[\s\S]*cccc_cache_ok)
//
// #1133: a CcccExecutable target caches per target (there is no per-source
// .o). Two Build() calls on the same target within one build_main: the
// second must report the target "(up to date)" and must not touch the
// binary. Mirrors test_build_link_staleness's double-Build pattern.

#include <stdio.h>
#include <sys/stat.h>

#ifdef __APPLE__
#define ST_MTIME(st) ((st).st_mtimespec.tv_sec)
#else
#define ST_MTIME(st) ((st).st_mtim.tv_sec)
#endif

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *app = CcccExecutable(ctx, "cccc_cache_app");
    AddSource(app, "examples/build_demo/src/main.c");
    AddSource(app, "examples/build_demo/src/greet.c");
    AddSource(app, "examples/build_demo/src/lib/sum.c");
    AddInclude(app, "examples/build_demo/include");

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
    printf("cccc_cache_ok\n");
    return 0;
}
