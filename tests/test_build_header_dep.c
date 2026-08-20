// CCCC_FLAGS: --build --build-cache=build/test_header_dep_cache
// --build-out-dir=build/test_header_dep_out CCCC_EXPECT_STDOUT: header_dep_ok
//
// #851: header-dependency (-MMD) tracking, and the CAS soundness rule that
// must accompany it. Runs three phases against the SAME target inside one
// build_main, via direct Build(ctx, target) calls (the serial dispatch path
// in run_graph() re-invokes build_target() unconditionally on each call, so
// this doubles as a controlled two-invocation harness without needing a
// second cccc process):
//
//   1. Build once.
//   2. Bump the header's mtime into the future (content unchanged) and
//      rebuild. Without .d tracking, ofile_is_current() only ever compared
//      main.o against main.c's mtime and would wrongly call main.o current
//      -- leaving it untouched. With tracking, the mtime check must at least
//      fall through to a real refresh (recompile or a legitimate CAS hit
//      keyed on unchanged header content); either way main.o's mtime must
//      advance.
//   3. Wipe the objdir (deleting its .d files) but keep the shared
//      --build-cache, change the header's real CONTENT, and rebuild. A
//      header-less cache key must never be looked up or stored -- otherwise
//      this fresh objdir could restore the stale object from step 1/2. The
//      resulting binary is actually executed and its exit code checked, so
//      a regression here fails loudly rather than merely "recompiling
//      unnecessarily often".

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <utime.h>
#include <time.h>

#ifdef __APPLE__
#define ST_MTIME(st) ((st).st_mtimespec.tv_sec)
#else
#define ST_MTIME(st) ((st).st_mtim.tv_sec)
#endif

[[cccc::build]]
int build_main(Builder *ctx) {
    const char *outdir = BuildOutDir(ctx);
    char        objdir[512], objpath[512], binpath[512];
    snprintf(objdir, sizeof(objdir), "%s/obj/hdrdep_app", outdir);
    snprintf(objpath, sizeof(objpath), "%s/main.o", objdir);

    WriteFile(ctx, "build/test_header_dep_src/hdr.h", "#define VAL 42\n");
    WriteFile(ctx, "build/test_header_dep_src/main.c",
              "#include \"hdr.h\"\nint main(void) { return VAL; }\n");

    BuildTarget *app = Executable(ctx, "hdrdep_app");
    AddSource(app, "build/test_header_dep_src/main.c");
    AddInclude(app, "build/test_header_dep_src");
    snprintf(binpath, sizeof(binpath), "%s", TargetOutput(app));

    // --- Phase 1: initial build ---
    if (Build(ctx, app) != 0)
        exit(1);
    struct stat st1;
    if (stat(objpath, &st1) != 0)
        exit(1);

    // Real wall-clock gap: the feature's own mtime comparisons are
    // second-granularity (plain time_t), so a same-second rebuild can't
    // distinguish "detected stale" from "wrongly treated as current".
    usleep(1200000);

    // --- Phase 2: header mtime bump (content unchanged) must not be
    // silently ignored. ---
    struct utimbuf future = {(time_t)(ST_MTIME(st1) + 120),
                             (time_t)(ST_MTIME(st1) + 120)};
    if (utime("build/test_header_dep_src/hdr.h", &future) != 0)
        exit(1);

    if (Build(ctx, app) != 0)
        exit(1);
    struct stat st2;
    if (stat(objpath, &st2) != 0)
        exit(1);
    if (ST_MTIME(st2) <= ST_MTIME(st1)) {
        printf("FAIL: object not refreshed after header mtime bump\n");
        exit(1);
    }

    // --- Phase 3: CAS soundness. Wipe the objdir (dropping its .d files)
    // while keeping the shared --build-cache, change the header's real
    // content, and rebuild. ---
    if (DeleteDir(ctx, objdir) != 0)
        exit(1);
    WriteFile(ctx, "build/test_header_dep_src/hdr.h", "#define VAL 7\n");

    if (Build(ctx, app) != 0)
        exit(1);

    int rc = system(binpath);
    if (rc == -1)
        exit(1);
    int exit_code = WEXITSTATUS(rc);
    if (exit_code != 7) {
        printf(
            "FAIL: binary exited %d, expected 7 (stale cache leaked through)\n",
            exit_code);
        exit(1);
    }

    printf("header_dep_ok\n");
    return 0;
}
