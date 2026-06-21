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

/*! @typedef Builder
 *  @abstract Opaque build context; passed to the entry and forwarded to macros. */
typedef struct Builder Builder;

/*! @typedef BuildTarget
 *  @abstract Opaque handle to a declared build target. */
typedef struct BuildTarget BuildTarget;

// ============================================================================
// Underlying FFI-callable functions
// ============================================================================

/*! @function __builtin_build_root
 *  @abstract Absolute path of the directory the build was launched from. */
const char *__builtin_build_root(Builder *ctx);

/*! @function __builtin_build_out_dir
 *  @abstract Output directory for build artifacts (default @c build/). */
const char *__builtin_build_out_dir(Builder *ctx);

/*! @function __builtin_build_host
 *  @abstract Host platform name (@c "darwin", @c "linux", ...). */
const char *__builtin_build_host(Builder *ctx);

/*! @function __builtin_build_verbose
 *  @abstract Non-zero when verbose output was requested. */
int __builtin_build_verbose(Builder *ctx);

/*! @function __builtin_build_executable
 *  @abstract Create an executable target named @c name (output @c bin/<name>). */
BuildTarget *__builtin_build_executable(Builder *ctx, const char *name);

/*! @function __builtin_build_static_lib
 *  @abstract Create a static-library target (output @c lib/lib<name>.a). */
BuildTarget *__builtin_build_static_lib(Builder *ctx, const char *name);

/*! @function __builtin_build_dynamic_lib
 *  @abstract Create a dynamic-library target (output @c lib/lib<name>.{so,dylib}). */
BuildTarget *__builtin_build_dynamic_lib(Builder *ctx, const char *name);

/*! @function __builtin_build_set_output
 *  @abstract Override the target's output path (relative to the out dir). */
void __builtin_build_set_output(BuildTarget *t, const char *path);

/*! @function __builtin_build_add_source
 *  @abstract Add a C source file to the target. */
void __builtin_build_add_source(BuildTarget *t, const char *path);

/*! @function __builtin_build_add_sources_glob
 *  @abstract Expand a glob pattern relative to the build root and add each
 *            match as a source file.  Requires POSIX @c glob(3). */
void __builtin_build_add_sources_glob(BuildTarget *t, const char *pattern);

/*! @function __builtin_build_add_source_str
 *  @abstract Write @c content to @c \<out_dir\>/gen/\<name\> and add it as a source.
 *            @c name must end in @c .c (or another compilable extension). */
void __builtin_build_add_source_str(BuildTarget *t, const char *name,
                                    const char *content);

/*! @function __builtin_build_exclude_source
 *  @abstract Exclude sources matching @c pattern (exact path or @c fnmatch glob)
 *            from compilation.  Applies after any @c AddSource / @c AddSourcesGlob
 *            calls, regardless of call order. */
void __builtin_build_exclude_source(BuildTarget *t, const char *pattern);

/*! @function __builtin_build_add_include
 *  @abstract Add an include search path (-I) to the target's compiles. */
void __builtin_build_add_include(BuildTarget *t, const char *path);

/*! @function __builtin_build_add_define
 *  @abstract Add a preprocessor define (-D). Pass NULL value for a bare define. */
void __builtin_build_add_define(BuildTarget *t, const char *name, const char *value);

/*! @function __builtin_build_add_undef
 *  @abstract Add a preprocessor undefine (-U). */
void __builtin_build_add_undef(BuildTarget *t, const char *name);

/*! @function __builtin_build_add_cflag
 *  @abstract Add a raw compiler flag (e.g. "-O2"). */
void __builtin_build_add_cflag(BuildTarget *t, const char *flag);

/*! @function __builtin_build_add_ldflag
 *  @abstract Add a raw linker flag. */
void __builtin_build_add_ldflag(BuildTarget *t, const char *flag);

/*! @function __builtin_build_link_with
 *  @abstract Declare that @c t links against (and is built after) @c dep.
 *            Adds a @c -l<dep> flag at link time. */
void __builtin_build_link_with(BuildTarget *t, BuildTarget *dep);

/*! @function __builtin_build_depends_on
 *  @abstract Ordering-only dependency: @c t is built after @c dep but does
 *            @b not add a @c -l<dep> linker flag.  Use this to order a
 *            @c RunCustom codegen step before its consumer. */
void __builtin_build_depends_on(BuildTarget *t, BuildTarget *dep);

/*! @function __builtin_build_add_lib
 *  @abstract Add a system library to link against (-l<name>). */
void __builtin_build_add_lib(BuildTarget *t, const char *name);

/*! @function __builtin_build_add_libpath
 *  @abstract Add a library search path (-L<path>). */
void __builtin_build_add_libpath(BuildTarget *t, const char *path);

/*! @function __builtin_build_have_tool
 *  @abstract Returns 1 if the named tool is executable (found in @c PATH) and
 *            permitted by the current tool allowlist, 0 otherwise. */
int __builtin_build_have_tool(Builder *ctx, const char *name);

/*! @function __builtin_build_pkg_config
 *  @abstract Run @c pkg-config to obtain compile and link flags for @c pkg and
 *            add them to @c t.  Returns 0 on success, non-zero if @c pkg-config
 *            is not found, not allowed, or reports an error. */
int __builtin_build_pkg_config(BuildTarget *t, const char *pkg);

/*! @function __builtin_build_run_custom
 *  @abstract Register a custom shell-command target named @c name that runs
 *            @c cmd when the target is reached in the build graph.  Returns an
 *            opaque @c BuildTarget* so other targets may declare a dependency on
 *            it via @c DependsOn.  Fails (non-zero exit from @c BuildDefault)
 *            if @c cmd returns a non-zero exit code. */
BuildTarget *__builtin_build_run_custom(Builder *ctx, const char *name,
                                        const char *cmd);

/*! @function __builtin_build_set_profile
 *  @abstract Set the build profile for target @c t, overriding any global
 *            @c --build-profile default.  Valid names: @c "debug",
 *            @c "release", @c "relwithdebinfo", @c "minsizerel".
 *
 *  Profile flags are prepended before the target's own @c AddCFlag entries,
 *  so per-target flags can override them:
 *
 *  | Profile       | Compiler flags | Defines   |
 *  |---------------|----------------|-----------|
 *  | debug         | -g -O0         | —         |
 *  | release       | -O2            | -DNDEBUG  |
 *  | relwithdebinfo| -O2 -g         | -DNDEBUG  |
 *  | minsizerel    | -Os            | -DNDEBUG  | */
void __builtin_build_set_profile(BuildTarget *t, const char *profile);

/*! @function __builtin_build_profile
 *  @abstract Returns the global build profile name (from @c --build-profile),
 *            or @c NULL if no global profile is set. */
const char *__builtin_build_profile(Builder *ctx);

/*! @function __builtin_build_target_count
 *  @abstract Returns the number of @c [[cccc::build_target]] factory functions
 *            declared in the current build script.  Useful for programmatic
 *            target enumeration from inside the build entry. */
int __builtin_build_target_count(Builder *ctx);

/*! @function __builtin_build_target_name
 *  @abstract Returns the name of the @c i -th (0-based) @c [[cccc::build_target]]
 *            factory, or @c NULL if @c i is out of range. */
const char *__builtin_build_target_name(Builder *ctx, int i);

/*! @function __builtin_build_run
 *  @abstract Build @c t and its transitive dependencies. Returns 0 on success. */
int __builtin_build_run(Builder *ctx, BuildTarget *t);

/*! @function __builtin_build_run_all
 *  @abstract Build every declared target in topological order. */
int __builtin_build_run_all(Builder *ctx);

/*! @function __builtin_build_run_default
 *  @abstract Build every declared target and print a summary. */
int __builtin_build_run_default(Builder *ctx);

// ============================================================================
// PascalCase macro wrappers (ctx is passed explicitly by the build entry)
// ============================================================================

#define BuildRoot(ctx)          __builtin_build_root(ctx)
#define BuildOutDir(ctx)        __builtin_build_out_dir(ctx)
#define BuildHost(ctx)          __builtin_build_host(ctx)
#define BuildVerbose(ctx)       __builtin_build_verbose(ctx)

#define Executable(ctx, name)   __builtin_build_executable(ctx, name)
#define StaticLib(ctx, name)    __builtin_build_static_lib(ctx, name)
#define DynamicLib(ctx, name)   __builtin_build_dynamic_lib(ctx, name)

#define SetOutput(t, p)         __builtin_build_set_output(t, p)
#define AddSource(t, p)         __builtin_build_add_source(t, p)
#define AddSourcesGlob(t, pat)  __builtin_build_add_sources_glob(t, pat)
#define AddSourceStr(t, n, c)   __builtin_build_add_source_str(t, n, c)
#define ExcludeSource(t, pat)   __builtin_build_exclude_source(t, pat)
#define AddInclude(t, p)        __builtin_build_add_include(t, p)
#define AddDefine(t, n, v)      __builtin_build_add_define(t, n, v)
#define AddUndef(t, n)          __builtin_build_add_undef(t, n)
#define AddCFlag(t, f)          __builtin_build_add_cflag(t, f)
#define AddLdFlag(t, f)         __builtin_build_add_ldflag(t, f)
#define LinkWith(t, dep)        __builtin_build_link_with(t, dep)
#define DependsOn(t, dep)       __builtin_build_depends_on(t, dep)
#define AddLib(t, n)            __builtin_build_add_lib(t, n)
#define AddLibPath(t, p)        __builtin_build_add_libpath(t, p)

#define HaveTool(ctx, name)     __builtin_build_have_tool(ctx, name)
#define PkgConfig(t, pkg)       __builtin_build_pkg_config(t, pkg)
#define RunCustom(ctx, name, cmd) __builtin_build_run_custom(ctx, name, cmd)

#define SetProfile(t, p)         __builtin_build_set_profile(t, p)
#define BuildProfile(ctx)        __builtin_build_profile(ctx)

#define BuildTargetCount(ctx)    __builtin_build_target_count(ctx)
#define BuildTargetName(ctx, i)  __builtin_build_target_name(ctx, i)

#define Build(ctx, t)           __builtin_build_run(ctx, t)
#define BuildAll(ctx)           __builtin_build_run_all(ctx)
#define BuildDefault(ctx)       __builtin_build_run_default(ctx)
