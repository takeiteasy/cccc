/*!
 * @file building.h
 * @brief CCCC Build System — builder API injected in --build mode.
 *
 * This header is automatically injected when CCCC is invoked with the @c --build
 * flag and should not be included directly.  It declares the opaque build types,
 * the underlying @c __builtin_build_* FFI-callable functions, and PascalCase macro
 * wrappers that forward an explicit build context parameter.
 *
 * A build script is an ordinary @c .c file containing a build entry — a function
 * tagged @c [[cccc::build]] (or named @c build_main).  The entry receives the
 * build context as its first parameter, uses it to create targets, wires their
 * dependencies, and calls one of the @c Build* functions.  The host-side runner
 * then compiles and links the declared targets with the system toolchain
 * (@c cc / @c ar / @c ld).
 *
 * ## Usage
 *
 * @code
 * [[cccc::build]]
 * int build_main(Builder *ctx) {
 *     BuildTarget *core = StaticLib(ctx, "core");
 *     AddSource(core, "src/lib/sum.c");
 *     AddInclude(core, "include");
 *
 *     BuildTarget *app = Executable(ctx, "app");
 *     AddSource(app, "src/main.c");
 *     LinkWith(app, core);
 *
 *     return BuildDefault(ctx);
 * }
 * @endcode
 *
 * A non-zero return from the entry signals build failure.
 */

#pragma once

// ============================================================================
// Opaque build types (owned by the VM / host runner)
// ============================================================================

/*! @brief Opaque build context; passed to the entry and forwarded to macros. */
typedef struct Builder Builder;

/*! @brief Opaque handle to a declared build target. */
typedef struct BuildTarget BuildTarget;

// ============================================================================
// Underlying FFI-callable functions
// ============================================================================

/*!
 * @brief Absolute path of the directory the build was launched from.
 * @param ctx Build context.
 * @return The build root, as an interned string.
 */
const char *__builtin_build_root(Builder *ctx);

/*!
 * @brief Output directory for build artifacts (default @c build/).
 * @param ctx Build context.
 * @return The output directory, as an interned string.
 */
const char *__builtin_build_out_dir(Builder *ctx);

/*!
 * @brief Host platform name (@c "darwin", @c "linux", ...).
 * @param ctx Build context.
 * @return The host platform name, as an interned string.
 */
const char *__builtin_build_host(Builder *ctx);

/*!
 * @brief Non-zero when verbose output was requested.
 * @param ctx Build context.
 * @return 1 if @c --build-verbose was passed, 0 otherwise.
 */
int __builtin_build_verbose(Builder *ctx);

/*!
 * @brief Create an executable target named @c name (output @c bin/\<name\>).
 * @param ctx Build context.
 * @param name Target name.
 * @return The new target handle.
 */
BuildTarget *__builtin_build_executable(Builder *ctx, const char *name);

/*!
 * @brief Create a static-library target (output @c lib/lib\<name\>.a).
 * @param ctx Build context.
 * @param name Target name.
 * @return The new target handle.
 */
BuildTarget *__builtin_build_static_lib(Builder *ctx, const char *name);

/*!
 * @brief Create a dynamic-library target (output @c lib/lib\<name\>.{so,dylib}).
 * @param ctx Build context.
 * @param name Target name.
 * @return The new target handle.
 */
BuildTarget *__builtin_build_dynamic_lib(Builder *ctx, const char *name);

/*!
 * @brief Override the target's output path (relative to the out dir).
 * @param t Target to modify.
 * @param path Output path, relative to the out dir.
 */
void __builtin_build_set_output(BuildTarget *t, const char *path);

/*!
 * @brief On-disk output path for @c t.
 * @details Lets a @c RunCustom command reference the binary a dependency
 *            target just built instead of hardcoding a path. For
 *            EXE/STATIC/DYNAMIC/BYTECODE targets this is
 *            @c \<out_dir\>/\<path\> — the explicit @c SetOutput() path if
 *            given, else the kind-appropriate default (@c bin/\<name\>,
 *            @c lib/lib\<name\>.a, ...). For a @c RunCustom target it is the
 *            @b first path @c DeclareOutput() recorded (call order), returned
 *            verbatim (@b not joined onto @c out_dir, since a custom command
 *            can write anywhere).
 * @param t Target to query.
 * @return The output path, or @c "" if @c DeclareOutput() was never called
 *            (@c RunCustom targets only).
 */
const char *__builtin_build_target_output(BuildTarget *t);

/*!
 * @brief Record a file path a @c RunCustom target (only) produces.
 * @details Taken verbatim, not joined onto @c out_dir, so @c TargetOutput()
 *            can resolve it, the dependency is documented for downstream
 *            consumers, and (combined with @c AddInput) @c build_target()
 *            can decide the step is already up to date. May be called more
 *            than once; each call appends rather than replacing, and
 *            @c TargetOutput() returns the first. On its own (no
 *            @c AddInput calls) this is invalidation metadata only — a
 *            @c RunCustom step with no declared inputs still runs every
 *            build regardless of whether @c path already exists. No-op with
 *            a diagnostic on non-@c RunCustom targets.
 * @param t Target to record against (must be a @c RunCustom target).
 * @param path Output path, taken verbatim.
 */
void __builtin_build_declare_output(BuildTarget *t, const char *path);

/*!
 * @brief Record a file path a @c RunCustom target (only) reads.
 * @details Combined with @c DeclareOutput, gives @c build_target() a real
 *            "up to date" skip check: if the target has at least one
 *            declared input and one declared output, and every output exists
 *            and is at least as new as every input, the command is skipped.
 *            A target with no @c AddInput calls always runs, as before.
 *            No-op with a diagnostic on non-@c RunCustom targets.
 * @param t Target to record against (must be a @c RunCustom target).
 * @param path Input path the command reads.
 */
void __builtin_build_add_input(BuildTarget *t, const char *path);

/*!
 * @brief Add a C source file to the target.
 * @param t Target to add the source to.
 * @param path Source file path, relative to the build root.
 */
void __builtin_build_add_source(BuildTarget *t, const char *path);

/*!
 * @brief Expand a glob pattern relative to the build root and add each
 *            match as a source file, immediately.
 * @details Requires POSIX @c glob(3). Matches are returned in sorted order.
 * @param t Target to add the matched sources to.
 * @param pattern Glob pattern, relative to the build root.
 */
void __builtin_build_add_sources_glob(BuildTarget *t, const char *pattern);

/*!
 * @brief Like @c AddSourcesGlob, but expansion happens at build time,
 *            after @c t's dependencies have already been built.
 * @details Lets the pattern match a file a @c RunCustom codegen dependency
 *            creates during this same build. @c AddSourcesGlob expands
 *            immediately and cannot see such files.
 * @param t Target to add the matched sources to.
 * @param pattern Glob pattern, relative to the build root.
 */
void __builtin_build_add_sources_glob_deferred(BuildTarget *t, const char *pattern);

/*!
 * @brief Write @c content to @c \<out_dir\>/gen/\<name\> and add it as a source.
 * @param t Target to add the generated source to.
 * @param name Generated file name; must end in @c .c (or another compilable
 *            extension).
 * @param content File contents to write.
 */
void __builtin_build_add_source_str(BuildTarget *t, const char *name,
                                    const char *content);

/*!
 * @brief Exclude sources matching @c pattern from compilation.
 * @details Applies after any @c AddSource / @c AddSourcesGlob calls,
 *            regardless of call order.
 * @param t Target to exclude the source from.
 * @param pattern Exact path or @c fnmatch glob to match against.
 */
void __builtin_build_exclude_source(BuildTarget *t, const char *pattern);

/*!
 * @brief Add an include search path (-I) to the target's compiles.
 * @param t Target to modify.
 * @param path Include search path.
 */
void __builtin_build_add_include(BuildTarget *t, const char *path);

/*!
 * @brief Add a preprocessor define (-D).
 * @param t Target to modify.
 * @param name Macro name.
 * @param value Macro value, or @c NULL for a bare define.
 */
void __builtin_build_add_define(BuildTarget *t, const char *name, const char *value);

/*!
 * @brief Add a preprocessor undefine (-U).
 * @param t Target to modify.
 * @param name Macro name to undefine.
 */
void __builtin_build_add_undef(BuildTarget *t, const char *name);

/*!
 * @brief Add a raw compiler flag (e.g. "-O2").
 * @param t Target to modify.
 * @param flag Flag to append.
 */
void __builtin_build_add_cflag(BuildTarget *t, const char *flag);

/*!
 * @brief Add a raw linker flag.
 * @param t Target to modify.
 * @param flag Flag to append.
 */
void __builtin_build_add_ldflag(BuildTarget *t, const char *flag);

/*!
 * @brief Set an environment variable for @c t's compiler/linker child
 *            process only (e.g. @c AFL_USE_ASAN=1 for an afl-asan target).
 * @details Has no effect on a @c RunCustom target, whose command runs
 *            through the vendored build shell, not the host toolchain.
 * @param t Target to modify.
 * @param name Environment variable name.
 * @param value Environment variable value.
 */
void __builtin_build_set_target_env(BuildTarget *t, const char *name, const char *value);

/*!
 * @brief Declare that @c t links against (and is built after) @c dep.
 * @details Adds a @c -l<dep> flag at link time.
 * @param t Target that links against @c dep.
 * @param dep Target being linked against.
 */
void __builtin_build_link_with(BuildTarget *t, BuildTarget *dep);

/*!
 * @brief Ordering-only dependency: @c t is built after @c dep but does
 *            @b not add a @c -l<dep> linker flag.
 * @details Use this to order a @c RunCustom codegen step before its
 *            consumer.
 * @param t Target that depends on @c dep.
 * @param dep Target that must build first.
 */
void __builtin_build_depends_on(BuildTarget *t, BuildTarget *dep);

/*!
 * @brief Add a system library to link against (@c -l\<name\>).
 * @param t Target to modify.
 * @param name Library name, without the @c lib prefix or extension.
 */
void __builtin_build_add_lib(BuildTarget *t, const char *name);

/*!
 * @brief Add a library search path (-L<path>).
 * @param t Target to modify.
 * @param path Library search path.
 */
void __builtin_build_add_libpath(BuildTarget *t, const char *path);

/*!
 * @brief Return the value of environment variable @c name.
 * @param ctx Build context.
 * @param name Environment variable name.
 * @return The variable's value, or @c NULL if unset.
 */
const char *__builtin_build_get_env(Builder *ctx, const char *name);

/*!
 * @brief Run @c cmd via @c sh @c -c and return its stdout.
 * @details Trailing whitespace is stripped. The returned pointer is valid
 *            until the build entry returns.
 * @param ctx Build context.
 * @param cmd Shell command to run.
 * @return Stdout as a NUL-terminated string, or @c NULL on failure or on
 *            non-POSIX platforms.
 */
const char *__builtin_build_capture_command(Builder *ctx, const char *cmd);

/*!
 * @brief Check whether a path exists.
 * @param ctx Build context.
 * @param path Path to check (file, directory, or any other filesystem node).
 * @return 1 if @c path exists, 0 otherwise.
 */
int __builtin_build_file_exists(Builder *ctx, const char *path);

/*!
 * @brief Check whether a named tool is available.
 * @param ctx Build context.
 * @param name Tool name to look up in @c PATH.
 * @return 1 if @c name is executable and permitted by the current tool
 *            allowlist, 0 otherwise.
 */
int __builtin_build_have_tool(Builder *ctx, const char *name);

/*!
 * @brief Run @c pkg-config to obtain compile and link flags for @c pkg and
 *            add them to @c t.
 * @param t Target to add the flags to.
 * @param pkg Package name to query.
 * @return 0 on success, non-zero if @c pkg-config is not found, not
 *            allowed, or reports an error.
 */
int __builtin_build_pkg_config(BuildTarget *t, const char *pkg);

/*!
 * @brief Register a custom shell-command target named @c name that runs
 *            @c cmd when the target is reached in the build graph.
 * @details Fails (non-zero exit from @c BuildDefault) if @c cmd returns a
 *            non-zero exit code.
 * @param ctx Build context.
 * @param name Target name.
 * @param cmd Shell command to run.
 * @return The new target handle, so other targets may declare a dependency
 *            on it via @c DependsOn.
 */
BuildTarget *__builtin_build_run_custom(Builder *ctx, const char *name,
                                        const char *cmd);

/*!
 * @brief Set the build profile for target @c t, overriding any global
 *            @c --build-profile default.
 * @details Profile flags are prepended before the target's own @c AddCFlag
 *            entries, so per-target flags can override them:
 *
 *  | Profile       | Compiler flags | Defines   |
 *  |---------------|----------------|-----------|
 *  | debug         | -g -O0         | —         |
 *  | release       | -O2            | -DNDEBUG  |
 *  | relwithdebinfo| -O2 -g         | -DNDEBUG  |
 *  | minsizerel    | -Os            | -DNDEBUG  |
 * @param t Target to modify.
 * @param profile Profile name: @c "debug", @c "release",
 *            @c "relwithdebinfo", or @c "minsizerel".
 */
void __builtin_build_set_profile(BuildTarget *t, const char *profile);

/*!
 * @brief Return the global build profile name.
 * @param ctx Build context.
 * @return The profile set via @c --build-profile, or @c NULL if none is set.
 */
const char *__builtin_build_profile(Builder *ctx);

/*!
 * @brief Override the compiler binary for @c t (e.g. @c "aarch64-linux-gnu-gcc").
 * @details Takes precedence over @c --build-cc and the system default. Use
 *            for GCC-style cross-compilers that are identified by a
 *            prefixed name.
 * @param t Target to modify.
 * @param cc Compiler binary name or path.
 */
void __builtin_build_set_toolchain(BuildTarget *t, const char *cc);

/*!
 * @brief Set a clang-style target triple for @c t (e.g. @c "aarch64-linux-gnu").
 * @details Appends @c --target=\<triple\> to both compile and link
 *            invocations.
 * @param t Target to modify.
 * @param triple Target triple.
 */
void __builtin_build_set_target_triple(BuildTarget *t, const char *triple);

/*!
 * @brief Return the global cross-compilation triple.
 * @param ctx Build context.
 * @return The triple set via @c --build-triple, or @c NULL if none is set.
 */
const char *__builtin_build_target_triple(Builder *ctx);

/*!
 * @brief Return the number of @c [[cccc::build_target]] factory functions
 *            declared in the current build script.
 * @details Useful for programmatic target enumeration from inside the
 *            build entry.
 * @param ctx Build context.
 * @return The number of declared @c [[cccc::build_target]] factories.
 */
int __builtin_build_target_count(Builder *ctx);

/*!
 * @brief Return the name of the @c i -th @c [[cccc::build_target]] factory.
 * @param ctx Build context.
 * @param i Zero-based factory index.
 * @return The factory's name, or @c NULL if @c i is out of range.
 */
const char *__builtin_build_target_name(Builder *ctx, int i);

/*!
 * @brief Return the full path of a named tool.
 * @details The returned pointer is valid until the build entry returns.
 * @param ctx Build context.
 * @param name Tool name to look up in @c PATH.
 * @return The tool's full path, or @c NULL if it is not executable in
 *            @c PATH or not permitted by the tool allowlist.
 */
const char *__builtin_build_find_tool(Builder *ctx, const char *name);

/*!
 * @brief Return the value of a @c --build-option=key=value option passed on
 *            the command line.
 * @param ctx Build context.
 * @param name Option key.
 * @return The option's value, or @c NULL if @c name was not given.
 */
const char *__builtin_build_get_build_option(Builder *ctx, const char *name);

/*!
 * @brief Check whether a @c --build-option was passed on the command line.
 * @param ctx Build context.
 * @param name Option key.
 * @return 1 if @c --build-option=key (or @c --build-option=key=...) was
 *            passed, 0 otherwise.
 */
int __builtin_build_have_build_option(Builder *ctx, const char *name);

/*!
 * @brief Add a macOS @c -framework @c Name linker flag.
 * @details Shorthand for @c AddLdFlag(t, "-framework Name").
 * @param t Target to modify.
 * @param name Framework name.
 */
void __builtin_build_add_framework(BuildTarget *t, const char *name);

/*!
 * @brief Number of positional arguments forwarded via @c -- on the CLI.
 * @param ctx Build context.
 * @return The argument count, or 0 if no @c -- separator was given.
 */
int __builtin_build_argc(Builder *ctx);

/*!
 * @brief Return the @c i -th positional argument forwarded via @c -- on
 *            the CLI.
 * @param ctx Build context.
 * @param i Zero-based argument index.
 * @return The argument string, or @c NULL if @c i is out of range.
 */
const char *__builtin_build_argv(Builder *ctx, int i);

/*!
 * @brief Override the install prefix for @c InstallArtifact.
 * @details Default: @c PREFIX env var or @c /usr/local.
 * @param ctx Build context.
 * @param path Install prefix path.
 */
void __builtin_build_set_install_prefix(Builder *ctx, const char *path);

/*!
 * @brief Register @c t for installation when @c --build-install is active.
 * @details A no-op if @c --build-install was not passed. The copy happens
 *            after the build entry returns and the build succeeded.
 * @param ctx Build context.
 * @param t Target to install.
 */
void __builtin_build_install_artifact(Builder *ctx, BuildTarget *t);

/*!
 * @brief Check whether install was requested.
 * @param ctx Build context.
 * @return 1 if @c --build-install was passed on the command line, 0 otherwise.
 */
int __builtin_build_wants_install(Builder *ctx);

/*!
 * @brief Check whether a path exists and is a directory.
 * @param ctx Build context.
 * @param path Path to check.
 * @return 1 if @c path exists and is a directory, 0 otherwise.
 */
int __builtin_build_dir_exists(Builder *ctx, const char *path);

/*!
 * @brief Expand @c pattern and return the matching paths.
 * @details Uses POSIX @c glob(3). The array and its strings are valid until
 *            the build entry returns.
 * @param ctx Build context.
 * @param pattern Glob pattern.
 * @return A @c NULL-terminated array of matching paths, or @c NULL on no
 *            match or non-POSIX platforms.
 */
const char **__builtin_build_glob_files(Builder *ctx, const char *pattern);

/*!
 * @brief Read the file at @c path into a string.
 * @details The returned pointer is valid until the build entry returns.
 * @param ctx Build context.
 * @param path File path to read.
 * @return The file contents as a NUL-terminated string, or @c NULL on
 *            error or if the file exceeds 4 MB.
 */
const char *__builtin_build_read_file(Builder *ctx, const char *path);

/*!
 * @brief Write @c content to @c path, creating parent directories as needed.
 * @param ctx Build context.
 * @param path File path to write.
 * @param content Content to write.
 * @return 0 on success, -1 on error.
 */
int __builtin_build_write_file(Builder *ctx, const char *path, const char *content);

/*!
 * @brief Change the process working directory to @c path.
 * @details The original CWD is saved on the first call and automatically
 *            restored when the build entry returns.
 * @param ctx Build context.
 * @param path Directory to change into.
 * @return 0 on success, -1 on error.
 * @note @c cd inside a @c RunCustom shell script does @b not affect the
 *        parent CWD (RunCustom runs in a forked child); @c SetCwd changes
 *        the real process CWD and is visible to all subsequent build steps.
 */
int __builtin_build_set_cwd(Builder *ctx, const char *path);

/*!
 * @brief Return the current process working directory.
 * @details The pointer is valid until the build entry returns.
 * @param ctx Build context.
 * @return The current working directory as an interned string, or @c NULL
 *            on error.
 */
const char *__builtin_build_get_cwd(Builder *ctx);

/*!
 * @brief Copy file @c src to @c dst.
 * @param ctx Build context.
 * @param src Source file path.
 * @param dst Destination file path.
 * @return 0 on success, -1 on error.
 */
int __builtin_build_copy_file(Builder *ctx, const char *src, const char *dst);

/*!
 * @brief Move (rename) file @c src to @c dst.
 * @details Falls back to copy + delete on cross-device moves.
 * @param ctx Build context.
 * @param src Source file path.
 * @param dst Destination file path.
 * @return 0 on success, -1 on error.
 */
int __builtin_build_move_file(Builder *ctx, const char *src, const char *dst);

/*!
 * @brief Delete the file at @c path (@c unlink).
 * @param ctx Build context.
 * @param path File path to delete.
 * @return 0 on success, -1 on error.
 */
int __builtin_build_delete_file(Builder *ctx, const char *path);

/*!
 * @brief Create @c path and all intermediate directories (@c mkdir @c -p
 *            semantics).
 * @param ctx Build context.
 * @param path Directory path to create.
 * @return 0 on success, -1 on error.
 */
int __builtin_build_mkdir(Builder *ctx, const char *path);

/*!
 * @brief Recursively delete @c path and all contents (@c rm @c -rf
 *            semantics).
 * @details Does not follow symlinks out of the tree.
 * @param ctx Build context.
 * @param path Directory path to delete.
 * @return 0 on success, -1 on error.
 */
int __builtin_build_delete_dir(Builder *ctx, const char *path);

/*!
 * @brief Build @c t and its transitive dependencies.
 * @param ctx Build context.
 * @param t Target to build.
 * @return 0 on success.
 */
int __builtin_build_run(Builder *ctx, BuildTarget *t);

/*!
 * @brief Build every declared target in topological order.
 * @param ctx Build context.
 * @return 0 on success.
 */
int __builtin_build_run_all(Builder *ctx);

/*!
 * @brief Build every declared target and print a summary.
 * @param ctx Build context.
 * @return 0 on success.
 */
int __builtin_build_run_default(Builder *ctx);

// ============================================================================
// PascalCase macro wrappers (ctx is passed explicitly by the build entry)
// ============================================================================

/*! @def BuildRoot
 * @brief Absolute path of the directory the build was launched from.
 * @param ctx Build context. */
#define BuildRoot(ctx)          __builtin_build_root(ctx)

/*! @def BuildOutDir
 * @brief Output directory for build artifacts (default @c build/).
 * @param ctx Build context. */
#define BuildOutDir(ctx)        __builtin_build_out_dir(ctx)

/*! @def BuildHost
 * @brief Host platform name (@c "darwin", @c "linux", ...).
 * @param ctx Build context. */
#define BuildHost(ctx)          __builtin_build_host(ctx)

/*! @def BuildVerbose
 * @brief Non-zero when verbose output was requested.
 * @param ctx Build context. */
#define BuildVerbose(ctx)       __builtin_build_verbose(ctx)

/*! @def Executable
 * @brief Create an executable target (output @c bin/\<name\>).
 * @param ctx Build context.
 * @param name Target name. */
#define Executable(ctx, name)   __builtin_build_executable(ctx, name)

/*! @def StaticLib
 * @brief Create a static-library target (output @c lib/lib\<name\>.a).
 * @param ctx Build context.
 * @param name Target name. */
#define StaticLib(ctx, name)    __builtin_build_static_lib(ctx, name)

/*! @def DynamicLib
 * @brief Create a dynamic-library target (output @c lib/lib\<name\>.{so,dylib}).
 * @param ctx Build context.
 * @param name Target name. */
#define DynamicLib(ctx, name)   __builtin_build_dynamic_lib(ctx, name)

/*! @def SetOutput
 * @brief Override the target's output path (relative to the out dir).
 * @param t Target to modify.
 * @param p Output path. */
#define SetOutput(t, p)         __builtin_build_set_output(t, p)

/*! @def TargetOutput
 * @brief On-disk output path for @c t.
 * @param t Target to query. */
#define TargetOutput(t)         __builtin_build_target_output(t)

/*! @def DeclareOutput
 * @brief Record a file path a @c RunCustom target (only) produces.
 * @param t Target to record against.
 * @param p Output path, taken verbatim. */
#define DeclareOutput(t, p)     __builtin_build_declare_output(t, p)

/*! @def AddSource
 * @brief Add a C source file to the target.
 * @param t Target to add the source to.
 * @param p Source file path, relative to the build root. */
#define AddSource(t, p)         __builtin_build_add_source(t, p)

/*! @def AddSourcesGlob
 * @brief Expand a glob pattern and add each match as a source file, immediately.
 * @param t Target to add the matched sources to.
 * @param pat Glob pattern, relative to the build root. */
#define AddSourcesGlob(t, pat)  __builtin_build_add_sources_glob(t, pat)

/*! @def AddSourcesGlobDeferred
 * @brief Like @c AddSourcesGlob, but expansion happens at build time,
 *            after @c t's dependencies have already been built.
 * @param t Target to add the matched sources to.
 * @param pat Glob pattern, relative to the build root. */
#define AddSourcesGlobDeferred(t, pat) __builtin_build_add_sources_glob_deferred(t, pat)

/*! @def AddSourceStr
 * @brief Write @c c to @c \<out_dir\>/gen/\<n\> and add it as a source.
 * @param t Target to add the generated source to.
 * @param n Generated file name; must end in @c .c (or another compilable extension).
 * @param c File contents to write. */
#define AddSourceStr(t, n, c)   __builtin_build_add_source_str(t, n, c)

/*! @def AddInput
 * @brief Record a file path a @c RunCustom target (only) reads.
 * @param t Target to record against.
 * @param p Input path the command reads. */
#define AddInput(t, p)          __builtin_build_add_input(t, p)

/*! @def ExcludeSource
 * @brief Exclude sources matching @c pat from compilation.
 * @param t Target to exclude the source from.
 * @param pat Exact path or @c fnmatch glob to match against. */
#define ExcludeSource(t, pat)   __builtin_build_exclude_source(t, pat)

/*! @def AddInclude
 * @brief Add an include search path (-I) to the target's compiles.
 * @param t Target to modify.
 * @param p Include search path. */
#define AddInclude(t, p)        __builtin_build_add_include(t, p)

/*! @def AddDefine
 * @brief Add a preprocessor define (-D).
 * @param t Target to modify.
 * @param n Macro name.
 * @param v Macro value, or @c NULL for a bare define. */
#define AddDefine(t, n, v)      __builtin_build_add_define(t, n, v)

/*! @def AddUndef
 * @brief Add a preprocessor undefine (-U).
 * @param t Target to modify.
 * @param n Macro name to undefine. */
#define AddUndef(t, n)          __builtin_build_add_undef(t, n)

/*! @def AddCFlag
 * @brief Add a raw compiler flag (e.g. "-O2").
 * @param t Target to modify.
 * @param f Flag to append. */
#define AddCFlag(t, f)          __builtin_build_add_cflag(t, f)

/*! @def AddLdFlag
 * @brief Add a raw linker flag.
 * @param t Target to modify.
 * @param f Flag to append. */
#define AddLdFlag(t, f)         __builtin_build_add_ldflag(t, f)

/*! @def SetTargetEnv
 * @brief Set an environment variable for @c t's compiler/linker child
 *            process only.
 * @param t Target to modify.
 * @param n Environment variable name.
 * @param v Environment variable value. */
#define SetTargetEnv(t, n, v)   __builtin_build_set_target_env(t, n, v)

/*! @def LinkWith
 * @brief Declare that @c t links against (and is built after) @c dep.
 * @param t Target that links against @c dep.
 * @param dep Target being linked against. */
#define LinkWith(t, dep)        __builtin_build_link_with(t, dep)

/*! @def DependsOn
 * @brief Ordering-only dependency: @c t is built after @c dep but does
 *            @b not add a linker flag.
 * @param t Target that depends on @c dep.
 * @param dep Target that must build first. */
#define DependsOn(t, dep)       __builtin_build_depends_on(t, dep)

/*! @def AddLib
 * @brief Add a system library to link against (@c -l\<name\>).
 * @param t Target to modify.
 * @param n Library name, without the @c lib prefix or extension. */
#define AddLib(t, n)            __builtin_build_add_lib(t, n)

/*! @def AddLibPath
 * @brief Add a library search path (-L<path>).
 * @param t Target to modify.
 * @param p Library search path. */
#define AddLibPath(t, p)        __builtin_build_add_libpath(t, p)

/*! @def GetEnv
 * @brief Return the value of environment variable @c name.
 * @param ctx Build context.
 * @param name Environment variable name. */
#define GetEnv(ctx, name)           __builtin_build_get_env(ctx, name)

/*! @def CaptureCommand
 * @brief Run @c cmd via @c sh @c -c and return its stdout.
 * @param ctx Build context.
 * @param cmd Shell command to run. */
#define CaptureCommand(ctx, cmd)    __builtin_build_capture_command(ctx, cmd)

/*! @def FileExists
 * @brief Check whether a path exists.
 * @param ctx Build context.
 * @param path Path to check (file, directory, or any other filesystem node). */
#define FileExists(ctx, path)       __builtin_build_file_exists(ctx, path)

/*! @def DirExists
 * @brief Check whether a path exists and is a directory.
 * @param ctx Build context.
 * @param path Path to check. */
#define DirExists(ctx, path)        __builtin_build_dir_exists(ctx, path)

/*! @def GlobFiles
 * @brief Expand @c pattern and return the matching paths.
 * @param ctx Build context.
 * @param pattern Glob pattern. */
#define GlobFiles(ctx, pattern)     __builtin_build_glob_files(ctx, pattern)

/*! @def ReadFile
 * @brief Read the file at @c path into a string.
 * @param ctx Build context.
 * @param path File path to read. */
#define ReadFile(ctx, path)         __builtin_build_read_file(ctx, path)

/*! @def WriteFile
 * @brief Write @c c to @c path, creating parent directories as needed.
 * @param ctx Build context.
 * @param path File path to write.
 * @param c Content to write. */
#define WriteFile(ctx, path, c)     __builtin_build_write_file(ctx, path, c)

/*! @def SetCwd
 * @brief Change the process working directory to @c path.
 * @param ctx Build context.
 * @param path Directory to change into. */
#define SetCwd(ctx, path)           __builtin_build_set_cwd(ctx, path)

/*! @def GetCwd
 * @brief Return the current process working directory.
 * @param ctx Build context. */
#define GetCwd(ctx)                 __builtin_build_get_cwd(ctx)

/*! @def CopyFile
 * @brief Copy file @c src to @c dst.
 * @param ctx Build context.
 * @param src Source file path.
 * @param dst Destination file path. */
#define CopyFile(ctx, src, dst)     __builtin_build_copy_file(ctx, src, dst)

/*! @def MoveFile
 * @brief Move (rename) file @c src to @c dst.
 * @param ctx Build context.
 * @param src Source file path.
 * @param dst Destination file path. */
#define MoveFile(ctx, src, dst)     __builtin_build_move_file(ctx, src, dst)

/*! @def DeleteFile
 * @brief Delete the file at @c path (@c unlink).
 * @param ctx Build context.
 * @param path File path to delete. */
#define DeleteFile(ctx, path)       __builtin_build_delete_file(ctx, path)

/*! @def MkDir
 * @brief Create @c path and all intermediate directories (@c mkdir @c -p
 *            semantics).
 * @param ctx Build context.
 * @param path Directory path to create. */
#define MkDir(ctx, path)            __builtin_build_mkdir(ctx, path)

/*! @def DeleteDir
 * @brief Recursively delete @c path and all contents (@c rm @c -rf semantics).
 * @param ctx Build context.
 * @param path Directory path to delete. */
#define DeleteDir(ctx, path)        __builtin_build_delete_dir(ctx, path)

/*! @def HaveTool
 * @brief Check whether a named tool is available.
 * @param ctx Build context.
 * @param name Tool name to look up in @c PATH. */
#define HaveTool(ctx, name)     __builtin_build_have_tool(ctx, name)

/*! @def FindTool
 * @brief Return the full path of a named tool.
 * @param ctx Build context.
 * @param name Tool name to look up in @c PATH. */
#define FindTool(ctx, name)     __builtin_build_find_tool(ctx, name)

/*! @def PkgConfig
 * @brief Run @c pkg-config to obtain compile and link flags for @c pkg and
 *            add them to @c t.
 * @param t Target to add the flags to.
 * @param pkg Package name to query. */
#define PkgConfig(t, pkg)       __builtin_build_pkg_config(t, pkg)

/*! @def RunCustom
 * @brief Register a custom shell-command target that runs @c cmd when
 *            reached in the build graph.
 * @param ctx Build context.
 * @param name Target name.
 * @param cmd Shell command to run. */
#define RunCustom(ctx, name, cmd) __builtin_build_run_custom(ctx, name, cmd)

/*! @def GetBuildOption
 * @brief Return the value of a @c --build-option=key=value option passed
 *            on the command line.
 * @param ctx Build context.
 * @param name Option key. */
#define GetBuildOption(ctx, name)    __builtin_build_get_build_option(ctx, name)

/*! @def HaveBuildOption
 * @brief Check whether a @c --build-option was passed on the command line.
 * @param ctx Build context.
 * @param name Option key. */
#define HaveBuildOption(ctx, name)   __builtin_build_have_build_option(ctx, name)

/*! @def AddFramework
 * @brief Add a macOS @c -framework @c Name linker flag.
 * @param t Target to modify.
 * @param name Framework name. */
#define AddFramework(t, name)        __builtin_build_add_framework(t, name)

/*! @def BuildArgc
 * @brief Number of positional arguments forwarded via @c -- on the CLI.
 * @param ctx Build context. */
#define BuildArgc(ctx)               __builtin_build_argc(ctx)

/*! @def BuildArgv
 * @brief Return the @c i -th positional argument forwarded via @c -- on
 *            the CLI.
 * @param ctx Build context.
 * @param i Zero-based argument index. */
#define BuildArgv(ctx, i)            __builtin_build_argv(ctx, i)

/*! @def SetInstallPrefix
 * @brief Override the install prefix for @c InstallArtifact.
 * @param ctx Build context.
 * @param path Install prefix path. */
#define SetInstallPrefix(ctx, path)  __builtin_build_set_install_prefix(ctx, path)

/*! @def InstallArtifact
 * @brief Register @c t for installation when @c --build-install is active.
 * @param ctx Build context.
 * @param t Target to install. */
#define InstallArtifact(ctx, t)      __builtin_build_install_artifact(ctx, t)

/*! @def BuildWantsInstall
 * @brief Check whether install was requested.
 * @param ctx Build context. */
#define BuildWantsInstall(ctx)       __builtin_build_wants_install(ctx)

/*! @def SetProfile
 * @brief Set the build profile for target @c t, overriding any global
 *            @c --build-profile default.
 * @param t Target to modify.
 * @param p Profile name: @c "debug", @c "release", @c "relwithdebinfo",
 *            or @c "minsizerel". */
#define SetProfile(t, p)              __builtin_build_set_profile(t, p)

/*! @def BuildProfile
 * @brief Return the global build profile name.
 * @param ctx Build context. */
#define BuildProfile(ctx)             __builtin_build_profile(ctx)

/*! @def SetToolchain
 * @brief Override the compiler binary for @c t (e.g. @c "aarch64-linux-gnu-gcc").
 * @param t Target to modify.
 * @param cc Compiler binary name or path. */
#define SetToolchain(t, cc)           __builtin_build_set_toolchain(t, cc)

/*! @def SetTargetTriple
 * @brief Set a clang-style target triple for @c t (e.g. @c "aarch64-linux-gnu").
 * @param t Target to modify.
 * @param triple Target triple. */
#define SetTargetTriple(t, triple)    __builtin_build_set_target_triple(t, triple)

/*! @def BuildTargetTriple
 * @brief Return the global cross-compilation triple.
 * @param ctx Build context. */
#define BuildTargetTriple(ctx)        __builtin_build_target_triple(ctx)

/*! @def BuildTargetCount
 * @brief Return the number of @c [[cccc::build_target]] factory functions
 *            declared in the current build script.
 * @param ctx Build context. */
#define BuildTargetCount(ctx)    __builtin_build_target_count(ctx)

/*! @def BuildTargetName
 * @brief Return the name of the @c i -th @c [[cccc::build_target]] factory.
 * @param ctx Build context.
 * @param i Zero-based factory index. */
#define BuildTargetName(ctx, i)  __builtin_build_target_name(ctx, i)

/*! @def Build
 * @brief Build @c t and its transitive dependencies.
 * @param ctx Build context.
 * @param t Target to build. */
#define Build(ctx, t)           __builtin_build_run(ctx, t)

/*! @def BuildAll
 * @brief Build every declared target in topological order.
 * @param ctx Build context. */
#define BuildAll(ctx)           __builtin_build_run_all(ctx)

/*! @def BuildDefault
 * @brief Build every declared target and print a summary.
 * @param ctx Build context. */
#define BuildDefault(ctx)       __builtin_build_run_default(ctx)
