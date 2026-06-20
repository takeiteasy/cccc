/*!
 * @file cccc_build.h
 * @brief CCCC Build System — builder API injected in --build mode.
 *
 * This header is automatically injected when CCCC is invoked with the @c --build
 * flag and should not be included directly.  It declares the opaque build types
 * and the builder API a build script uses to declare native targets.
 *
 * A build script is an ordinary @c .c file containing a build entry — a function
 * tagged @c [[cccc::build]] (or named @c build_main).  The entry runs inside the
 * CCCC VM, imperatively creates targets, wires their dependencies, and calls one
 * of the @c cccc_build_run* functions.  The host-side runner then compiles and
 * links the declared targets with the system toolchain (@c cc / @c ar / @c ld).
 *
 * ## Usage
 *
 * @code
 * [[cccc::build]]
 * int build_main(cccc_build_ctx_t *ctx) {
 *     cccc_target_t *core = cccc_static_lib(ctx, "core");
 *     cccc_target_add_source(core, "src/lib/sum.c");
 *     cccc_target_add_include(core, "include");
 *
 *     cccc_target_t *app = cccc_executable(ctx, "app");
 *     cccc_target_add_source(app, "src/main.c");
 *     cccc_target_link_with(app, core);
 *
 *     return cccc_build_run_default(ctx);
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
 *  @abstract Opaque build context passed to the entry and the builder API. */
typedef struct cccc_build_ctx_t cccc_build_ctx_t;

/*! @typedef cccc_target_t
 *  @abstract Opaque handle to a declared build target. */
typedef struct cccc_target_t cccc_target_t;

// ============================================================================
// Build context
// ============================================================================

/*! @function cccc_build_root
 *  @abstract Absolute path of the directory the build was launched from. */
const char *cccc_build_root(cccc_build_ctx_t *ctx);

/*! @function cccc_build_out_dir
 *  @abstract Output directory for build artifacts (default @c build/). */
const char *cccc_build_out_dir(cccc_build_ctx_t *ctx);

/*! @function cccc_build_host
 *  @abstract Host platform name (@c "darwin", @c "linux", ...). */
const char *cccc_build_host(cccc_build_ctx_t *ctx);

/*! @function cccc_build_verbose
 *  @abstract Non-zero when verbose output was requested. */
int cccc_build_verbose(cccc_build_ctx_t *ctx);

// ============================================================================
// Target factories — each returns a target owned by ctx
// ============================================================================

/*! @function cccc_executable
 *  @abstract Create an executable target named @c name (output @c bin/<name>). */
cccc_target_t *cccc_executable(cccc_build_ctx_t *ctx, const char *name);

/*! @function cccc_static_lib
 *  @abstract Create a static-library target (output @c lib/lib<name>.a). */
cccc_target_t *cccc_static_lib(cccc_build_ctx_t *ctx, const char *name);

/*! @function cccc_dynamic_lib
 *  @abstract Create a dynamic-library target (output @c lib/lib<name>.{so,dylib}). */
cccc_target_t *cccc_dynamic_lib(cccc_build_ctx_t *ctx, const char *name);

// ============================================================================
// Output
// ============================================================================

/*! @function cccc_target_set_output
 *  @abstract Override the target's output path (relative to the out dir). */
void cccc_target_set_output(cccc_target_t *t, const char *path);

// ============================================================================
// Sources
// ============================================================================

/*! @function cccc_target_add_source
 *  @abstract Add a C source file to the target. */
void cccc_target_add_source(cccc_target_t *t, const char *path);

// ============================================================================
// Flags
// ============================================================================

/*! @function cccc_target_add_include
 *  @abstract Add an include search path (-I) to the target's compiles. */
void cccc_target_add_include(cccc_target_t *t, const char *path);

/*! @function cccc_target_add_define
 *  @abstract Add a preprocessor define (-D). Pass NULL value for a bare define. */
void cccc_target_add_define(cccc_target_t *t, const char *name, const char *value);

/*! @function cccc_target_add_undef
 *  @abstract Add a preprocessor undefine (-U). */
void cccc_target_add_undef(cccc_target_t *t, const char *name);

/*! @function cccc_target_add_cflag
 *  @abstract Add a raw compiler flag (e.g. "-O2"). */
void cccc_target_add_cflag(cccc_target_t *t, const char *flag);

/*! @function cccc_target_add_ldflag
 *  @abstract Add a raw linker flag. */
void cccc_target_add_ldflag(cccc_target_t *t, const char *flag);

// ============================================================================
// Dependencies
// ============================================================================

/*! @function cccc_target_link_with
 *  @abstract Declare that @c t links against (and is built after) @c dep. */
void cccc_target_link_with(cccc_target_t *t, cccc_target_t *dep);

/*! @function cccc_target_add_lib
 *  @abstract Add a system library to link against (-l<name>). */
void cccc_target_add_lib(cccc_target_t *t, const char *name);

/*! @function cccc_target_add_libpath
 *  @abstract Add a library search path (-L<path>). */
void cccc_target_add_libpath(cccc_target_t *t, const char *path);

// ============================================================================
// Run — synchronously compiles and links the requested target(s)
// ============================================================================

/*! @function cccc_build_run
 *  @abstract Build @c t and its transitive dependencies. Returns 0 on success. */
int cccc_build_run(cccc_build_ctx_t *ctx, cccc_target_t *t);

/*! @function cccc_build_run_all
 *  @abstract Build every declared target in topological order. */
int cccc_build_run_all(cccc_build_ctx_t *ctx);

/*! @function cccc_build_run_default
 *  @abstract Build every declared target and print a summary. */
int cccc_build_run_default(cccc_build_ctx_t *ctx);
