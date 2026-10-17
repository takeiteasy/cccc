// CCCC_FLAGS: --build --build-cache=build/test_cccc_object_cache_cache --build-out-dir=build/test_cccc_object_cache_out
// CCCC_EXPECT_STDOUT: \(up to date\) cccc_obj_cache_sum
// CCCC_EXPECT_STDOUT: \(up to date\) cccc_obj_cache_app
// CCCC_EXPECT_STDOUT: cccc_object_cache_ok
//
// A CcccObject and the CcccExecutable that links it cache per target: an
// unchanged second Build() is up to date, and editing the object's source
// rebuilds the object and relinks the consumer.

#include <stdio.h>
#include <string.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    const char *src = "build/test_cccc_object_cache_src/val.c";
    WriteFile(ctx, src, "int val(void) { return 1; }\n");
    WriteFile(ctx, "build/test_cccc_object_cache_src/main.c",
              "#include <stdio.h>\nint val(void);\n"
              "int main(void) { printf(\"%d\\n\", val()); return 0; }\n");

    BuildTarget *obj = CcccObject(ctx, "cccc_obj_cache_sum");
    AddSource(obj, src);
    BuildTarget *app = CcccExecutable(ctx, "cccc_obj_cache_app");
    AddSource(app, "build/test_cccc_object_cache_src/main.c");
    LinkWith(app, obj);

    if (Build(ctx, app) != 0)
        return 1;
    const char *out = CaptureCommand(ctx, TargetOutput(app));
    if (!out || strncmp(out, "1", 1) != 0) {
        printf("FAIL: expected 1\n");
        return 1;
    }
    if (Build(ctx, app) != 0)
        return 1;

    WriteFile(ctx, src, "int val(void) { return 2; }\n");
    if (Build(ctx, app) != 0)
        return 1;
    out = CaptureCommand(ctx, TargetOutput(app));
    if (!out || strncmp(out, "2", 1) != 0) {
        printf("FAIL: consumer not relinked after the object changed\n");
        return 1;
    }
    printf("cccc_object_cache_ok\n");
    return 0;
}
