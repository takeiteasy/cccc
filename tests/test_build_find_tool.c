// CCCC_FLAGS: --build
//
// FindTool: return the full executable path or NULL.
// Unlike HaveTool, FindTool returns the path string so the caller can embed
// it in a define or pass it to RunCustom.

[[cccc::build]]
int build_main(Builder *ctx) {
    // "sh" exists on all supported platforms.
    const char *sh = FindTool(ctx, "sh");
    if (!sh || sh[0] != '/')
        return 1;

    // A made-up name must return NULL.
    const char *nope = FindTool(ctx, "cccc_tool_that_does_not_exist_xyz999");
    if (nope != (void *)0)
        return 1;

    return 42;
}
