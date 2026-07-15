/*
 CCCC: Comprehensiev C Compensation Compiler

 Copyright (C) 2025 George Watson

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

// Declarations needed by the CLI driver (main.c) that aren't part of the
// public cccc.h API. Included by internal.h so every other TU still sees
// them; main.c includes cccc.h + driver.h directly instead of internal.h
// (#664).

#pragma once

#include "cccc.h"

#ifndef __has_include
#define __has_include(x) 0
#endif

#if __has_include(<stdnoreturn.h>)
#include <stdnoreturn.h>
#else
#define noreturn
#endif

#ifndef __attribute__
#define __attribute__(x)
#endif

//
// tokenize.c
//

noreturn void error(char *fmt, ...) __attribute__((format(printf, 1, 2)));
uint64_t cccc_warning_mask_for_name(const char *name);
bool cccc_warning_is_group_name(const char *name);
void cc_output_preprocessed(FILE *f, VirtualMachine *vm, Token *tok);

//
// preprocess.c
//

char *cccc_find_native_cc(void);
int cc_rehydrate_asm_passthru(VirtualMachine *vm);
void init_mode_macros(VirtualMachine *vm);
void define_std_macros(VirtualMachine *vm);

//
// parse.c
//

bool is_flonum(Type *ty);

//
// vm.c
//

void cc_vm_profile_print(VirtualMachine *vm, FILE *f);
int cc_vm_profile_write_json(VirtualMachine *vm, FILE *f, const char *mode,
                             const char *input_name);
long long generate_random_canary(void);

//
// analyze.c
//

typedef struct {
    int n;        // 2 or 3
    int top_n;
    bool per_file;
} CcAnalyzeNgramOptions;

typedef struct {
    int top_n;
    bool json;    // emit JSON instead of text
} CcAnalyzeFusionOptions;

typedef struct CcNgramState CcNgramState;
typedef struct CcFusionState CcFusionState;

CcNgramState *cc_analyze_ngram_begin(const CcAnalyzeNgramOptions *opts);
void cc_analyze_ngram_feed(CcNgramState *st, const InstrWord *text,
                           long long num_words, const char *label, FILE *out);
void cc_analyze_ngram_finish(CcNgramState *st, FILE *out);

CcFusionState *cc_analyze_fusion_begin(const CcAnalyzeFusionOptions *opts);
void cc_analyze_fusion_feed(CcFusionState *st, const InstrWord *text,
                            long long num_words, const char *label,
                            FILE *out);
void cc_analyze_fusion_finish(CcFusionState *st, FILE *out);

//
// host_backtrace.c
//

/* Initialise libbacktrace state and warm up DWARF/Mach-O caches.
 * Must be called once at process startup (not in a signal handler) before
 * cc_host_backtrace_install_fatal().  argv0 is used to locate the binary. */
void cc_host_backtrace_init(const char *argv0);

/* Install top-level crash handlers (SIGSEGV/SIGBUS/SIGFPE/SIGILL) that print
 * a host C backtrace to stderr then re-raise the signal so the process dies
 * with the original signal/exit code.  No-op when CCCC_HAS_BACKTRACE is off
 * or on Windows. */
void cc_host_backtrace_install_fatal(void);

//
// dump_ast.c
//

void cc_dump_ast(FILE *f, Obj *prog, int verbose);
void cc_dump_ast_json(FILE *f, Obj *prog, int verbose);

//
// testing.c / debugger.c
//

void   cc_load_test_runtime(VirtualMachine *vm);
void   cc_load_symbolize_runtime(VirtualMachine *vm);
Token *cc_inject_test_header(VirtualMachine *vm);
int    cc_run_tests(VirtualMachine *vm, Obj *prog, const CcTestOptions *opts);

// native.c / main.c shared infrastructure
typedef struct {
    const char **data;
    int          len;
    int          cap;
} ArgVec;

void   argv_push(ArgVec *args, const char *arg);
char  *make_tmp_path(const char *suffix);
int    run_argv(char *const argv[]);

// Native compile flags extracted from a CCCC vm instance.
typedef struct {
    const char **inc_paths;       int inc_paths_count;
    const char **sys_inc_paths;   int sys_inc_paths_count;
    const char **lib_paths;       int lib_paths_count;
    const char **libs;            int libs_count;
    const char **defines;         int defines_count;
    const char **undefs;          int undefs_count;
    const char  *std_arg;
} CcNativeCompileArgs;

//
// build.c (--build mode)
//
typedef struct {
    const char *entry_name;             // --build-entry override, or NULL
    const char *target_name;            // --build-target=NAME, or NULL (build all)
    const char *out_dir;                // -O/--build-out-dir, or NULL (default "build")
    int         verbose;                // -v (also enables host-runner verbose output)
    int         build_verbose;          // --build-verbose: per-target headers + command lines
    int         quiet;                  // --build-quiet: suppress per-step command lines
    int         keep_going;             // --build-keep-going: continue past target failures
    int         dry_run;                // --build-dry-run: print commands, run nothing
    int         jobs;                   // --build-jobs=N: parallel source compile slots (0/1 = serial)
    const CcNativeCompileArgs *defaults; // CLI -I/-D/-U/--std forwarded to each target
    const char **tool_allow;            // --build-tool-allow names (NULL = allow-all)
    int          tool_allow_count;
    int          list_targets;          // --build-list-targets: print factory names and exit
    const char  *profile;               // --build-profile=NAME: debug|release|relwithdebinfo|minsizerel
    const char  *cross_triple;          // --build-triple=TRIPLE: clang-style cross target triple (#547)
    const char  *cross_cc;              // --build-cc=COMPILER: override CC binary globally (#547)
    const char  *build_cache;           // --build-cache[=PATH]: NULL=off, ""=default path, else given path (#546)
    const char  *cccc_self;             // path to the cccc binary (argv[0]); used for kind=bytecode targets (#545)
    const char **build_options;         // --build-option=key=value strings (#559)
    int          build_options_count;
    int          build_install;         // --build-install: copy artifacts after build (#560)
    const char **user_args;            // positional args after -- on the CLI (#558)
    int          user_args_count;
} CcBuildOptions;

void   cc_load_build_runtime(VirtualMachine *vm);
Token *cc_inject_build_header(VirtualMachine *vm);
int    cc_run_build(VirtualMachine *vm, Obj *prog, const CcBuildOptions *opts);
