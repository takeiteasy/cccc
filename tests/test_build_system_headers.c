// CCCC_FLAGS: --build --build-out-dir=build/test_system_headers_out
// CCCC_EXPECT_STDOUT: host_sysinc_ok
// CCCC_EXPECT_STDOUT: cccc_sysinc_ok
// CCCC_EXPECT_STDOUT: build succeeded
//
// AddSystemInclude reaches a host Executable (-isystem) and a CcccExecutable
// (--isystem); UseSystemHeaders compiles the latter against the host headers.

#include <stdio.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    WriteFile(ctx, "build/gen/sysinc/oro_sys.h", "#define ORO_SYS_VALUE 7\n");
    WriteFile(ctx, "build/gen/sysinc_main.c",
              "#include <stdio.h>\n"
              "#include <oro_sys.h>\n"
              "int main(void) {\n"
              "    printf(\"%s_sysinc_ok\\n\", ORO_SYS_VALUE == 7 ? "
              "SYSINC_NAME : \"bad\");\n"
              "    return 0;\n"
              "}\n");

    BuildTarget *host = Executable(ctx, "sysinc_host");
    AddSource(host, "build/gen/sysinc_main.c");
    AddSystemInclude(host, "build/gen/sysinc");
    AddDefine(host, "SYSINC_NAME", "\"host\"");

    BuildTarget *self = CcccExecutable(ctx, "sysinc_cccc");
    AddSource(self, "build/gen/sysinc_main.c");
    AddSystemInclude(self, "build/gen/sysinc");
    AddDefine(self, "SYSINC_NAME", "\"cccc\"");
    UseSystemHeaders(self);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s && %s", TargetOutput(host),
             TargetOutput(self));
    BuildTarget *run = RunCustom(ctx, "run-sysinc", cmd);
    DependsOn(run, host);
    DependsOn(run, self);

    return BuildDefault(ctx);
}
