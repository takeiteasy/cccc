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
 * int build_main(cccc_build_ctx_t *ctx) {
 *     cccc_target_t *core = StaticLib(ctx, "core");
 *     AddSource(core, "src/lib/sum.c");
 *     AddInclude(core, "include");
 *
 *     cccc_target_t *app = Executable(ctx, "app");
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

/*! @typedef cccc_build_ctx_t
 *  @abstract Opaque build context; passed to the entry and forwarded to macros. */
typedef struct cccc_build_ctx_t cccc_build_ctx_t;

/*! @typedef cccc_target_t
 *  @abstract Opaque handle to a declared build target. */
typedef struct cccc_target_t cccc_target_t;

// ============================================================================
// Underlying FFI-callable functions
// ============================================================================

/*! @function __builtin_build_root
 *  @abstract Absolute path of the directory the build was launched from. */
const char *__builtin_build_root(cccc_build_ctx_t *ctx);

/*! @function __builtin_build_out_dir
 *  @abstract Output directory for build artifacts (default @c build/). */
const char *__builtin_build_out_dir(cccc_build_ctx_t *ctx);

/*! @function __builtin_build_host
 *  @abstract Host platform name (@c "darwin", @c "linux", ...). */
const char *__builtin_build_host(cccc_build_ctx_t *ctx);

/*! @function __builtin_build_verbose
 *  @abstract Non-zero when verbose output was requested. */
int __builtin_build_verbose(cccc_build_ctx_t *ctx);

/*! @function __builtin_build_executable
 *  @abstract Create an executable target named @c name (output @c bin/<name>). */
cccc_target_t *__builtin_build_executable(cccc_build_ctx_t *ctx, const char *name);

/*! @function __builtin_build_static_lib
 *  @abstract Create a static-library target (output @c lib/lib<name>.a). */
cccc_target_t *__builtin_build_static_lib(cccc_build_ctx_t *ctx, const char *name);

/*! @function __builtin_build_dynamic_lib
 *  @abstract Create a dynamic-library target (output @c lib/lib<name>.{so,dylib}). */
cccc_target_t *__builtin_build_dynamic_lib(cccc_build_ctx_t *ctx, const char *name);

/*! @function __builtin_build_set_output
 *  @abstract Override the target's output path (relative to the out dir). */
void __builtin_build_set_output(cccc_target_t *t, const char *path);

/*! @function __builtin_build_add_source
 *  @abstract Add a C source file to the target. */
void __builtin_build_add_source(cccc_target_t *t, const char *path);

/*! @function __builtin_build_add_include
 *  @abstract Add an include search path (-I) to the target's compiles. */
void __builtin_build_add_include(cccc_target_t *t, const char *path);

/*! @function __builtin_build_add_define
 *  @abstract Add a preprocessor define (-D). Pass NULL value for a bare define. */
void __builtin_build_add_define(cccc_target_t *t, const char *name, const char *value);

/*! @function __builtin_build_add_undef
 *  @abstract Add a preprocessor undefine (-U). */
void __builtin_build_add_undef(cccc_target_t *t, const char *name);

/*! @function __builtin_build_add_cflag
 *  @abstract Add a raw compiler flag (e.g. "-O2"). */
void __builtin_build_add_cflag(cccc_target_t *t, const char *flag);

/*! @function __builtin_build_add_ldflag
 *  @abstract Add a raw linker flag. */
void __builtin_build_add_ldflag(cccc_target_t *t, const char *flag);

/*! @function __builtin_build_link_with
 *  @abstract Declare that @c t links against (and is built after) @c dep. */
void __builtin_build_link_with(cccc_target_t *t, cccc_target_t *dep);

/*! @function __builtin_build_add_lib
 *  @abstract Add a system library to link against (-l<name>). */
void __builtin_build_add_lib(cccc_target_t *t, const char *name);

/*! @function __builtin_build_add_libpath
 *  @abstract Add a library search path (-L<path>). */
void __builtin_build_add_libpath(cccc_target_t *t, const char *path);

/*! @function __builtin_build_run
 *  @abstract Build @c t and its transitive dependencies. Returns 0 on success. */
int __builtin_build_run(cccc_build_ctx_t *ctx, cccc_target_t *t);

/*! @function __builtin_build_run_all
 *  @abstract Build every declared target in topological order. */
int __builtin_build_run_all(cccc_build_ctx_t *ctx);

/*! @function __builtin_build_run_default
 *  @abstract Build every declared target and print a summary. */
int __builtin_build_run_default(cccc_build_ctx_t *ctx);

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
#define AddInclude(t, p)        __builtin_build_add_include(t, p)
#define AddDefine(t, n, v)      __builtin_build_add_define(t, n, v)
#define AddUndef(t, n)          __builtin_build_add_undef(t, n)
#define AddCFlag(t, f)          __builtin_build_add_cflag(t, f)
#define AddLdFlag(t, f)         __builtin_build_add_ldflag(t, f)
#define LinkWith(t, dep)        __builtin_build_link_with(t, dep)
#define AddLib(t, n)            __builtin_build_add_lib(t, n)
#define AddLibPath(t, p)        __builtin_build_add_libpath(t, p)

#define Build(ctx, t)           __builtin_build_run(ctx, t)
#define BuildAll(ctx)           __builtin_build_run_all(ctx)
#define BuildDefault(ctx)       __builtin_build_run_default(ctx)
