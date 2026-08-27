// CCCC_FLAGS: --build --build-cache=build/test_link_staleness_cache --build-out-dir=build/test_link_staleness_out
// CCCC_EXPECT_STDOUT: (?=[\s\S]*\(up to date\) linkstale_app)(?=[\s\S]*link_staleness_ok)
//
// #851: link-step staleness. No target had any incremental check at the
// link/archive step before this; a native EXE relinked unconditionally
// every build even when every .o and the link command line were unchanged.
// Two Build() calls on the same target within one
// build_main (the serial run_graph dispatch path re-invokes build_target()
// unconditionally each call): the second must report the link step
// "(up to date)" and must not touch the binary at all.

#include <stdio.h>
#include <sys/stat.h>

#ifdef __APPLE__
#define ST_MTIME(st) ((st).st_mtimespec.tv_sec)
#else
#define ST_MTIME(st) ((st).st_mtim.tv_sec)
#endif

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *app = Executable(ctx, "linkstale_app");
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
        printf("FAIL: binary relinked when nothing changed\n");
        return 1;
    }

    printf("link_staleness_ok\n");
    return 0;
}
