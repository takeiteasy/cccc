// CCCC_FLAGS: --build --build-install --build-out-dir=/tmp/cccc_test_install_559
// CCCC_EXPECT_STDOUT: install_test_app
//
// InstallArtifact / SetInstallPrefix / BuildWantsInstall:
// Register an artifact for installation and verify the install message appears.

[[cccc::build]]
int build_main(Builder *ctx) {
    // Redirect install to a temp directory so we don't touch the real system.
    SetInstallPrefix(ctx, "/tmp/cccc_install_prefix_559");

    // BuildWantsInstall should be 1 since we passed --build-install.
    if (!BuildWantsInstall(ctx)) return 1;

    BuildTarget *app = Executable(ctx, "install_test_app");
    AddSourceStr(app, "install_test_app.c",
                 "int main(void) { return 0; }\n");

    // Register the artifact for installation (no-op without --build-install).
    InstallArtifact(ctx, app);

    return BuildDefault(ctx);
}
