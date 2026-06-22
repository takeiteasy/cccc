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

#include "./internal.h"
#include <getopt.h>
#if defined(_WIN32)
#include <io.h>
#define CCCC_ISATTY _isatty
#define CCCC_FILENO _fileno
#else
#include <fnmatch.h>
#include <sys/wait.h>
#include <unistd.h>
#define CCCC_ISATTY isatty
#define CCCC_FILENO fileno
#endif

void argv_push(ArgVec *args, const char *arg) {
    if (args->len + 1 >= args->cap) {
        int new_cap = args->cap ? args->cap * 2 : 16;
        const char **new_data = realloc(args->data, sizeof(char *) * new_cap);
        if (!new_data)
            error("failed to allocate argument vector");
        args->data = new_data;
        args->cap = new_cap;
    }
    args->data[args->len++] = arg;
    args->data[args->len] = NULL;
}

char *make_tmp_path(const char *suffix) {
#if defined(_WIN32)
    (void)suffix;
    return NULL;
#else
    char template[] = "/tmp/cccc-native-XXXXXX";
    int fd = mkstemp(template);
    if (fd < 0)
        return NULL;
    close(fd);

    size_t len = strlen(template) + strlen(suffix) + 1;
    char *path = malloc(len);
    if (!path) {
        unlink(template);
        return NULL;
    }
    snprintf(path, len, "%s%s", template, suffix);
    if (rename(template, path) != 0) {
        unlink(template);
        free(path);
        return NULL;
    }
    return path;
#endif
}

int run_argv(char *const argv[]) {
#if defined(_WIN32)
    (void)argv;
    fprintf(stderr, "error: -c=native is not supported on Windows yet\n");
    return 1;
#else
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "error: failed to fork native compiler: %s\n",
                strerror(errno));
        return 1;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        fprintf(stderr, "error: failed to execute %s: %s\n", argv[0],
                strerror(errno));
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "error: failed to wait for %s: %s\n", argv[0],
                strerror(errno));
        return 1;
    }
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return 1;
#endif
}

static int run_native_backend(VirtualMachine *vm, Obj *prog, const char *out_file,
                              const char **inc_paths, int inc_paths_count,
                              const char **sys_inc_paths,
                              int sys_inc_paths_count, const char **lib_paths,
                              int lib_paths_count, const char **libs,
                              int libs_count, const char **defines,
                              int defines_count, const char **undefs,
                              int undefs_count, const char *std_arg) {
    if (!out_file) {
        fprintf(stderr,
                "error: -c=native requires -o <file> (no executable path given)\n");
        return 1;
    }

    char *cc = cccc_find_native_cc();
    if (!cc)
        return 1;

    char *source_path = make_tmp_path(".c");
    if (!source_path) {
        fprintf(stderr, "error: failed to create native source file\n");
        free(cc);
        return 1;
    }

    char *exe_path = strdup(out_file);
    if (!exe_path) {
        fprintf(stderr, "error: failed to duplicate native output path\n");
        unlink(source_path);
        free(source_path);
        free(cc);
        return 1;
    }

    FILE *f = fopen(source_path, "w");
    if (!f) {
        fprintf(stderr, "error: failed to open %s: %s\n", source_path,
                strerror(errno));
        unlink(source_path);
        free(source_path);
        free(exe_path);
        free(cc);
        return 1;
    }
    cc_serialize_program(f, vm, prog, false);
    if (fclose(f) != 0) {
        fprintf(stderr, "error: failed to write %s: %s\n", source_path,
                strerror(errno));
        unlink(source_path);
        free(source_path);
        free(exe_path);
        free(cc);
        return 1;
    }

    ArgVec cc_args = {0};
    argv_push(&cc_args, cc);
    argv_push(&cc_args, source_path);
    argv_push(&cc_args, "-o");
    argv_push(&cc_args, exe_path);
    if (std_arg) {
        char std_flag[256];
        snprintf(std_flag, sizeof(std_flag), "-std=%s", std_arg);
        argv_push(&cc_args, std_flag);
    }
    for (int i = 0; i < inc_paths_count; i++) {
        argv_push(&cc_args, "-I");
        argv_push(&cc_args, inc_paths[i]);
    }
    for (int i = 0; i < sys_inc_paths_count; i++) {
        argv_push(&cc_args, "-isystem");
        argv_push(&cc_args, sys_inc_paths[i]);
    }
    for (int i = 0; i < defines_count; i++) {
        char flag[256];
        snprintf(flag, sizeof(flag), "-D%s", defines[i]);
        argv_push(&cc_args, flag);
    }
    for (int i = 0; i < undefs_count; i++) {
        char flag[256];
        snprintf(flag, sizeof(flag), "-U%s", undefs[i]);
        argv_push(&cc_args, flag);
    }
    for (int i = 0; i < lib_paths_count; i++) {
        argv_push(&cc_args, "-L");
        argv_push(&cc_args, lib_paths[i]);
    }
    for (int i = 0; i < libs_count; i++) {
        char flag[256];
        snprintf(flag, sizeof(flag), "-l%s", libs[i]);
        argv_push(&cc_args, flag);
    }

    int rc = run_argv((char *const *)cc_args.data);

    unlink(source_path);
    free(cc_args.data);
    free(source_path);
    free(exe_path);
    free(cc);
    return rc;
}

static void usage(const char *argv0, int exit_code) {
    printf("CCCC: Comprehensiev C Compensation Compiler\n");
    printf("https://git.sr.ht/~takeiteasy/cccc\n\n");
    printf("Usage: %s [options] file...\n\n", argv0);
    printf("Options:\n");
    printf("\t-h/--help                Show this message\n");
    printf("\t-I <path>                Add <path> to include search paths\n");
    printf("\t-i/--isystem <path>      Add <path> to system include paths (for "
           "non-standard headers)\n");
    printf("\t-L/--library-path <path> Add <path> to dynamic library search paths\n");
    printf("\t-l/--library <name>      Link dynamic library by name or path\n");
    printf("\t-D <macro>[=def]         Define a macro\n");
    printf("\t-U <macro>               Undefine a macro\n");
    printf("\t-a/--ast                 Dump AST\n");
    printf("\t-p/--print-tokens        Print preprocessed tokens to stdout\n");
    printf("\t-E/--preprocess          Output preprocessed source code (traditional "
           "C -E)\n");
    printf("\t-m/--dump-expanded       Output macro-expanded source code (for gcc "
           "compatibility)\n");
    printf("\t-G/--emit-generated      Serialize runtime TU + macro-generated objects to C\n");
    printf("\t   --emit-only           With -G: only emit explicitly tagged content "
           "([[cccc::emit]], $publish)\n");
    printf("\t   --attr-target=TARGET  Attribute spelling in generated output: "
           "auto, c23, gnu, msvc, strip\n");
    printf("\t-j/--json                Emit JSON for all eligible output "
           "(diagnostics, header declarations, --fusion-candidates, etc.)\n");
    printf("\t-f/--ffi-decls           Emit parsed function/struct/enum declarations "
            "as JSON (for FFI wrapper generation)\n");
    printf("\t-X/--no-preprocess       Disable preprocessing step\n");
    printf("\t-S/--no-stdlib           Do not link standard library\n");
    printf("\t-c[FMT]/--compile[=FMT]  Compile only; do not execute. FMT: bytecode (default), native\n");
    printf("\t                         bytecode: write .c4 (to -o file, or stdout if -o omitted\n");
    printf("\t                                   and stdout is not a TTY)\n");
    printf("\t                         native: require -o file; build a native executable via\n");
    printf("\t                                 CCCC_NATIVE_CC (cc, clang, or gcc)\n");
    printf("\t                         Use -cnative or --compile=native (short form must be\n");
    printf("\t                         attached; long form may use '=' or separate arg).\n");
    printf("\t-o/--out <file>          Output file. Required for -c=native. For -c=bytecode, writes\n");
    printf("\t                         bytecode to <file>; if omitted, writes to stdout\n");
    printf("\t-d/--disassemble         Disassemble bytecode to stdout\n");
    printf("\t-t/--testing             Discover and run [[cccc::test]] functions\n");
    printf("\t   --test=GLOB           Run only tests whose name matches GLOB (implies --testing)\n");
    printf("\t   --test-suite=NAME     Run tests in NAME and its sub-suites (prefix match);\n");
    printf("\t                         glob metacharacters (*?[) switch to fnmatch (implies --testing)\n");
    printf("\t   --list-tests          List test names without running (implies --testing)\n");
    printf("\t   --fail-fast           Stop after the first failing test\n");
    printf("\t   --test-timeout=N      Per-test timeout in seconds (0 = no timeout;\n");
    printf("\t                         individual tests may override via\n");
    printf("\t                         [[cccc::test(timeout = ms)]])\n");
    printf("\t   --test-format=FMT     Output format for test results: tap (default), plain, json\n");
    printf("\t-v/--verbose             Enable debug logging\n");
    printf("\t-g/--debug               Enable interactive debugger\n");
    printf("\t   --no-debug-on-crash   Disable auto-drop into debugger on crash (for test harnesses)\n");
    printf("\t-e/--entry <name>        Set the entry-point function (default: main)\n");
    printf("\t   --vm-profile          Count executed VM opcodes and print a report\n");
    printf("\t                         Combine with --json to also dump the profile as JSON to stdout\n");
    printf("\nBuild Options:\n");
    printf("\t-b/--build               Run the input as a build script (declares native targets)\n");
    printf("\t   --build-entry=NAME    Build entry function to invoke (default: build_main)\n");
    printf("\t   --build-out-dir=PATH  Output directory for build artifacts (default: build/)\n");
    printf("\t   --build-dry-run       Print the toolchain command lines without executing them\n");
    printf("\t   --build-target=NAME   Build only the named target and its transitive dependencies\n");
    printf("\t   --build-tool-allow=N  Allowlist of tool names runnable via RunCustom/HaveTool/PkgConfig\n");
    printf("\t                         Accepts comma-separated or repeated flags. Default: allow all.\n");
    printf("\t   --build-jobs=N        Compile up to N source files in parallel per target (default: 1)\n");
    printf("\t   --build-keep-going    Continue building independent targets after a failure\n");
    printf("\t   --build-quiet         Suppress per-step command lines; only show errors and summary\n");
    printf("\t   --build-verbose       Print per-target headers and all command lines\n");
    printf("\t   --build-list-targets  List [[cccc::build_target]] factory names and exit\n");
    printf("\t   --build-profile=NAME  Set build profile: debug | release | relwithdebinfo | minsizerel\n");
    printf("\t   --build-triple=TRIPLE Cross-compile target triple (e.g. aarch64-linux-gnu; clang only)\n");
    printf("\t   --build-cc=COMPILER   Override CC binary for all targets (e.g. aarch64-linux-gnu-gcc)\n");
    printf("\t   --build-cache[=PATH]  Enable incremental builds: mtime+content-hash cache.\n");
    printf("\t                         Default cache dir: <out-dir>/.cccc-cache\n");
    printf("\t   --build-option=K=V    Pass a typed build option to the build script (GetBuildOption/HaveBuildOption).\n");
    printf("\t                         Accepts repeated flags: --build-option=foo=bar --build-option=baz=1\n");
    printf("\t   --build-install       After a successful build copy artifacts registered with InstallArtifact\n");
    printf("\t                         to the install prefix (default: PREFIX env var or /usr/local).\n");
    printf("\t   -- [args...]          Forward positional args to the build entry (BuildArgc/BuildArgv).\n");
    printf("\nWarning Options:\n");
    printf("\t-Wall               Enable common warning categories\n");
    printf("\t-Wextra             Enable extra warning categories\n");
    printf("\t-W<name>            Enable a warning category\n");
    printf("\t-Wno-<name>         Disable a warning category\n");
    printf("\t-w/--Werror         Treat enabled warnings as errors\n");
    printf("\t-Werror=<name>      Treat one warning category as an error\n");
    printf("\t-Wno-error=<name>   Do not promote one warning category\n");
    printf("\nSafety Levels (preset flag combinations):\n");
    printf("\t-0/--safety=none     No safety checks (maximum performance)\n");
    printf("\t-1/--safety=basic    Essential low-overhead checks (~5-10%% "
           "overhead)\n");
    printf("\t-2/--safety=standard Comprehensive development safety (~20-40%% "
           "overhead)\n");
    printf("\t-3/--safety=max      All safety features for deep debugging "
           "(~60-100%%+ overhead)\n");
    printf("\nMemory Safety Options (can be combined with safety levels):\n");
    printf("\t-B/--bounds-checks           Runtime array bounds checking\n");
    printf("\t   --uaf-detection           Use-after-free detection\n");
    printf("\t-C/--control-flow-integrity  Control-flow integrity (indirect call "
            "validation)\n");
    printf("\t   --type-checks             Runtime type checking on pointer "
            "dereferences\n");
    printf("\t   --uninitialized-detection Uninitialized variable detection\n");
    printf("\t   --overflow-checks         Detect signed integer overflow\n");
    printf("\t   --stack-canaries          Stack overflow protection\n");
    printf("\t   --heap-canaries           Heap overflow protection\n");
    printf("\t-M/--memory-leak-detection   Track allocations and report leaks "
            "at exit\n");
    printf("\t   --stack-instrumentation   Track stack variable lifetimes and "
            "accesses\n");
    printf("\t   --stack-errors            Enable runtime errors for stack "
           "instrumentation\n");
    printf("\t-P/--pointer-sanitizer       Enable all pointer checks (bounds, "
            "UAF, type)\n");
    printf("\t   --dangling-pointers       Detect use of stack pointers after "
           "function return\n");
    printf(
        "\t   --alignment-checks        Validate pointer alignment for type\n");
    printf("\t   --provenance-tracking     Track pointer origin and validate "
           "operations\n");
    printf("\t   --invalid-arithmetic      Detect pointer arithmetic outside "
           "object bounds\n");
    printf("\t   --format-string-checks    Validate format strings in "
            "printf-family functions\n");
    printf("\t   --random-canaries         Use random stack canaries (prevents "
            "predictable bypass)\n");
    printf("\t   --memory-poisoning        Poison allocated/freed memory "
           "(0xCD/0xDD patterns)\n");
    printf("\t   --memory-tagging          Temporal memory tagging (track "
            "pointer generation tags)\n");
    printf("\t-T/--thread-safety           Threading safety diagnostics: race "
           "detection, lock-order\n"
           "\t                             inversion, double-lock, and atomic "
           "cast warnings\n");
    printf("\t-V/--vm-heap                 Route all malloc/free through VM "
           "heap (enables memory safety)\n");
    printf("\nFFI Safety Options:\n");
    printf("\t   --ffi-allow=list       Allow only comma-separated native function names\n");
    printf("\t   --ffi-deny=list        Deny comma-separated native function names\n");
    printf("\t-F/--disable-ffi          Block all registered and dynamic native calls\n");
    printf("\t   --ffi-errors-fatal     Abort execution on FFI policy violations\n");
    printf("\t   --ffi-type-checking    Validate registered FFI call arity at runtime\n");
    printf("\nLanguage Standard:\n");
    printf("\t-s/--std=<std>       Select C language standard (default: gnu17)\n");
    printf("\t                     Supported: c99, c11, c17/c18, c23/c2x\n");
    printf("\t                     GNU variants: gnu99, gnu11, gnu17/gnu18, gnu23/gnu2x\n");
    printf("\t                     Note: -s/--std currently affects predefined macros only\n");
    printf("\nPreprocessor Options:\n");
    printf("\t   --embed-limit=SIZE         Set #embed file size warning limit "
            "(e.g., 50MB, 100mb, default: 10MB)\n");
    printf("\t   --embed-hard-limit         Make #embed limit a hard error "
            "instead of warning\n");
    printf("\t-r/--macro-recursion-limit=N  Limit recursive pragma macro "
            "expansion (default: 256, 0=unlimited)\n");
    printf("\t-n/--max-errors=N             Cap diagnostics at N (default: 20)\n");
    printf("\t   --comptime-include-all     Forward all #include'd declarations to the\n");
    printf("\t                              comptime pass (legacy behavior; default is\n");
    printf("\t                              runtime-only; use #include @shared to opt in\n");
    printf("\t                              individual headers)\n");
    printf("\t   --allow-comptime-pp-bleed  Allow #define/#undef inside one\n");
    printf("\t                              [[cccc::comptime]] function body to remain\n");
    printf("\t                              visible to other comptime function bodies\n");
    printf("\t                              (pre-#283 behavior; default is isolated)\n");
    printf("\nOptimization Levels:\n");
    printf("\t-O/--optimize[=LEVEL]        Enable bytecode optimization "
           "(default: disabled)\n");
    printf("\t                             LEVEL: 0=none, 1=basic, 2=standard, "
           "3=aggressive, 4=fused\n");
    printf("\t                             0: No optimization\n");
    printf("\t                             1: Constant folding only\n");
    printf("\t                             2: Constant folding + peephole\n");
    printf("\t                             3: All optimizations (including "
            "dead code elimination)\n");
    printf("\t                             4: Level 3 + automatic fused-op pass\n");
    printf("\t   --fuse-ops              Run automatic opcode fusion pass\n");
    printf("\t   --fma                   Enable single-rounding FMA (implies --fuse-ops; may change FP results)\n");
    printf("\t   --inline-limit=N        Limit inlining to N AST nodes "
            "(default: 256)\n");
    printf("\nStatic Bytecode Analysis (compile or load input, walk text "
           "segment, exit):\n");
    printf("\t   --ngrams[=N]            Static opcode n-gram analysis (N=2 or 3, "
            "default 2)\n");
    printf("\t   --ngrams-top=N          Show top N sequences (default 25)\n");
    printf("\t   --ngrams-per-file       Print a per-input section in addition "
           "to the aggregate\n");
    printf("\t   --fusion-candidates[=N] Use-def fusion candidate analysis (top "
            "N, default 50)\n");
    printf("\t                          JSON output via -j/--json\n");
    printf("\nInline Assembly:\n");
    printf("\t-A/--asm-passthru   Compile asm(\"...\") statements via native C compiler\n");
    printf("\t                    and execute them via FFI (default: no-op)\n");
    printf("\nExample:\n");
    printf("\t%s -o hello hello.c\n", argv0);
    printf("\t%s -I ./include -D DEBUG -o prog prog.c\n", argv0);
    printf("\techo 'int main() { return 42; }' | %s -\n", argv0);
    printf("\n");
    exit(exit_code);
}

static void configure_ffi_name_list(VirtualMachine *vm, const char *list,
                                    void (*add)(VirtualMachine *, const char *)) {
    const char *p = list;
    while (p && *p) {
        while (*p == ',' || isspace((unsigned char)*p))
            p++;
        const char *start = p;
        while (*p && *p != ',')
            p++;
        const char *end = p;
        while (end > start && isspace((unsigned char)end[-1]))
            end--;
        if (end > start) {
            size_t len = (size_t)(end - start);
            char *name = malloc(len + 1);
            if (!name)
                error("failed to allocate FFI policy name");
            memcpy(name, start, len);
            name[len] = '\0';
            add(vm, name);
            free(name);
        }
        if (*p == ',')
            p++;
    }
}

static char *find_requested_library(const char *name, const char **paths,
                                    int paths_count) {
    if (!name)
        return NULL;

    if (strchr(name, '/') || strstr(name, ".dylib") || strstr(name, ".so") ||
        strstr(name, ".dll"))
        return strdup(name);

#if defined(_WIN32) || defined(_WIN64)
    const char *prefix = "";
    const char *suffix = ".dll";
#elif defined(__APPLE__)
    const char *prefix = "lib";
    const char *suffix = ".dylib";
#else
    const char *prefix = "lib";
    const char *suffix = ".so";
#endif

    char libname[512];
    snprintf(libname, sizeof(libname), "%s%s%s", prefix, name, suffix);

    for (int i = 0; i < paths_count; i++) {
        char candidate[1024];
        snprintf(candidate, sizeof(candidate), "%s/%s", paths[i], libname);
        if (access(candidate, F_OK) == 0)
            return strdup(candidate);
    }

    return strdup(libname);
}

static int load_requested_libraries(VirtualMachine *vm, const char **libs, int libs_count,
                                    const char **paths, int paths_count) {
    for (int i = 0; i < libs_count; i++) {
        char *path = find_requested_library(libs[i], paths, paths_count);
        if (!path)
            return -1;
        int rc = cc_dlopen(vm, path);
        free(path);
#if defined(__linux__)
        // Development linker names such as libm.so may be linker scripts that
        // dlopen cannot consume. Fall back to the conventional runtime SONAME.
        if (rc != 0 && !strchr(libs[i], '/') && !strstr(libs[i], ".so")) {
            char soname[512];
            snprintf(soname, sizeof(soname), "lib%s.so.6", libs[i]);
            rc = cc_dlopen(vm, soname);
        }
#endif
        if (rc != 0)
            return -1;
    }
    return 0;
}

static int ffi_index_by_name(VirtualMachine *vm, const char *name) {
    size_t len = strlen(name);
    for (int i = 0; i < vm->compiler.ffi_count; i++) {
        if (vm->compiler.ffi_table[i].name &&
            vm->compiler.ffi_table[i].name_len == len &&
            memcmp(vm->compiler.ffi_table[i].name, name, len) == 0)
            return i;
    }
    return -1;
}

static const char *obj_external_name(Obj *obj) {
    return obj && obj->asm_label ? obj->asm_label : obj ? obj->name : NULL;
}

static int count_params(Type *ty) {
    int n = 0;
    for (Type *p = ty; p; p = p->next)
        n++;
    return n;
}

static void register_dynamic_externs(VirtualMachine *vm, Obj *prog) {
    for (Obj *obj = prog; obj; obj = obj->next) {
        const char *extern_name = obj_external_name(obj);
        if (!obj->is_function || obj->is_definition || !extern_name ||
            ffi_index_by_name(vm, extern_name) >= 0)
            continue;

        int nargs = count_params(obj->ty->params);
        int returns_double = is_flonum(obj->ty->return_ty);
        if (obj->ty->is_variadic)
            cc_register_variadic_cfunc(vm, extern_name, (void *)1, nargs,
                                       returns_double);
        else
            cc_register_cfunc(vm, extern_name, (void *)1, nargs, returns_double);
        vm->compiler.ffi_table[vm->compiler.ffi_count - 1]
            .is_dynamic_placeholder = 1;
    }
}

static int verify_dynamic_externs(VirtualMachine *vm) {
    int ok = 1;
    for (int i = 0; i < vm->compiler.ffi_count; i++) {
        ForeignFunc *ff = &vm->compiler.ffi_table[i];
        if (ff->is_dynamic_placeholder &&
            (!ff->func_ptr || ff->func_ptr == (void *)1)) {
            fprintf(stderr,
                    "error: unresolved dynamic library symbol '%s'\n",
                    ff->name);
            ok = 0;
        }
    }
    return ok ? 0 : -1;
}

static char *read_stdin_to_tmp(void) {
#if defined(_WIN32)
    char tmpPath[MAX_PATH + 1];
    char tmpFile[MAX_PATH + 1];
    DWORD len = GetTempPathA(MAX_PATH, tmpPath);
    if (len == 0 || len > MAX_PATH)
        return NULL;
    if (GetTempFileNameA(tmpPath, "asi", 0, tmpFile) == 0)
        return NULL;
    HANDLE h =
        CreateFileA(tmpFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return NULL;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) {
        DWORD written = 0;
        if (!WriteFile(h, buf, (DWORD)n, &written, NULL) ||
            written != (DWORD)n) {
            CloseHandle(h);
            DeleteFileA(tmpFile);
            return NULL;
        }
    }
    if (ferror(stdin)) {
        CloseHandle(h);
        DeleteFileA(tmpFile);
        return NULL;
    }
    CloseHandle(h);
    return _strdup(tmpFile);
#else
    char template[] = "/tmp/cccc-stdin-XXXXXX";
    int fd = mkstemp(template);
    if (fd < 0)
        return NULL;
    char buf[4096];
    ssize_t n;
    while ((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
        ssize_t w = 0;
        while (w < n) {
            ssize_t m = write(fd, buf + w, n - w);
            if (m < 0) {
                close(fd);
                unlink(template);
                return NULL;
            }
            w += m;
        }
    }
    if (n < 0) {
        close(fd);
        unlink(template);
        return NULL;
    }
    if (close(fd) < 0) {
        unlink(template);
        return NULL;
    }
    return strdup(template);
#endif
}

static void parse_define(VirtualMachine *vm, char *arg) {
    char *eq = strchr(arg, '=');
    if (eq) {
        *eq = '\0';
        cc_define(vm, arg, eq + 1);
    } else
        cc_define(vm, arg, "1");
}

static size_t parse_size(const char *str, const char *flag_name) {
    char *endptr;
    double value = strtod(str, &endptr);

    if (value < 0) {
        fprintf(stderr, "error: %s must be non-negative\n", flag_name);
        exit(1);
    }

    size_t multiplier = 1;
    if (*endptr != '\0') {
        // Parse suffix (KB, MB, GB, etc.)
        if (strcasecmp(endptr, "kb") == 0 || strcasecmp(endptr, "k") == 0) {
            multiplier = 1024;
        } else if (strcasecmp(endptr, "mb") == 0 ||
                   strcasecmp(endptr, "m") == 0) {
            multiplier = 1024 * 1024;
        } else if (strcasecmp(endptr, "gb") == 0 ||
                   strcasecmp(endptr, "g") == 0) {
            multiplier = 1024 * 1024 * 1024;
        } else if (strcasecmp(endptr, "b") == 0) {
            multiplier = 1; // Bytes
        } else {
            fprintf(stderr,
                    "error: invalid size suffix '%s' for %s (use KB, MB, GB, "
                    "or B)\n",
                    endptr, flag_name);
            exit(1);
        }
    }

    return (size_t)(value * multiplier);
}

static void parse_warning_option(const char *arg, uint64_t *warnings,
                                 uint64_t *warning_errors,
                                 uint64_t *warning_no_errors,
                                 uint64_t *warning_sticky_errors,
                                 int *warnings_as_errors) {
    if (strcmp(arg, "error") == 0) {
        *warnings_as_errors = 1;
        *warning_no_errors = 0;
        return;
    }

    if (strncmp(arg, "error=", 6) == 0) {
        const char *name = arg + 6;
        uint64_t mask = cccc_warning_mask_for_name(name);
        if (!mask || cccc_warning_is_group_name(name)) {
            fprintf(stderr, "error: unknown warning option '-Werror=%s'\n", name);
            exit(1);
        }
        *warnings |= mask;
        *warning_errors |= mask;
        *warning_no_errors &= ~mask;
        *warning_sticky_errors |= mask;
        return;
    }

    if (strncmp(arg, "no-error=", 9) == 0) {
        const char *name = arg + 9;
        uint64_t mask = cccc_warning_mask_for_name(name);
        if (!mask || cccc_warning_is_group_name(name)) {
            fprintf(stderr, "error: unknown warning option '-Wno-error=%s'\n", name);
            exit(1);
        }
        // -Werror=<name> is sticky: a later -Wno-error=<name> cannot demote it.
        // Use -Wno-<name> to fully disable (clearing the sticky bit too).
        if (!(*warning_sticky_errors & mask)) {
            *warning_errors &= ~mask;
            *warning_no_errors |= mask;
        }
        return;
    }

    bool disable = false;
    const char *name = arg;
    if (strncmp(arg, "no-", 3) == 0) {
        disable = true;
        name = arg + 3;
    }

    uint64_t mask = cccc_warning_mask_for_name(name);
    if (!mask) {
        fprintf(stderr, "error: unknown warning option '-W%s'\n", arg);
        exit(1);
    }

    if (disable) {
        *warnings &= ~mask;
        *warning_errors &= ~mask;
        *warning_no_errors &= ~mask;
        *warning_sticky_errors &= ~mask;
    } else {
        *warnings |= mask;
    }
}

static char **build_source_argv(int *prog_argc, int argc, const char *argv[],
                                int optind, int dashdash) {
    int prog_start;
    if (dashdash >= 0) {
        prog_start = dashdash + 1;
        *prog_argc = argc - dashdash; // argv[0] + everything after "--"
    } else {
        prog_start = optind;
        *prog_argc = argc - optind + 1;
    }

    char **prog_argv = malloc(sizeof(char *) * (size_t)*prog_argc);
    if (!prog_argv)
        error("out of memory");
    prog_argv[0] = (char *)argv[0];
    for (int i = 1; i < *prog_argc; i++)
        prog_argv[i] = (char *)argv[prog_start + i - 1];
    return prog_argv;
}

static char **build_c4_argv(int *prog_argc, const char *input_file, int argc,
                             const char *argv[], int dashdash) {
    if (dashdash >= 0)
        *prog_argc = argc - dashdash; // input_file + everything after "--"
    else
        *prog_argc = 1;

    char **prog_argv = malloc(sizeof(char *) * (size_t)*prog_argc);
    if (!prog_argv)
        error("out of memory");
    prog_argv[0] = (char *)input_file;
    for (int i = 1; i < *prog_argc; i++)
        prog_argv[i] = (char *)argv[dashdash + i];
    return prog_argv;
}

int main(int argc, const char *argv[]) {
    /* Initialise and install libbacktrace crash handler as early as possible
     * so that any fault during parse/codegen/VM produces a symbolic host
     * C stack trace to stderr before the process dies. */
    cc_host_backtrace_init(argv[0]);
    cc_host_backtrace_install_fatal();

    int exit_code = 0;
    const char **input_files = NULL;
    int input_files_count = 0;
    Obj **volatile input_progs = NULL;
    Token **volatile input_tokens = NULL;
    const char **inc_paths = NULL; // -I
    int inc_paths_count = 0;
    const char **sys_inc_paths = NULL; // -isystem
    int sys_inc_paths_count = 0;
    const char **lib_paths = NULL; // -L / --library-path
    int lib_paths_count = 0;
    const char **libs = NULL; // --library
    int libs_count = 0;
    const char **defines = NULL; // -D
    int defines_count = 0;
    const char **undefs = NULL; // -U
    int undefs_count = 0;
    char *out_file = NULL;     // -o (single)
    int dump_ast = 0;          // -a
    int disassemble = 0;       // -d
    int verbose = 0;           // -v
    uint32_t flags = 0;        // CCCCFlags bitfield for runtime features
    uint32_t cli_flags_mask = 0; // Bits explicitly set via CLI; wins over #pragma cccc config(...) (#357)
    bool cli_opt_level_set = false; // True if -O/--optimize was passed on the CLI (#357)
    int print_tokens = 0;      // -P
    int preprocess_only = 0;   // -E
    int dump_expanded_only = 0; // -M
    int emit_generated_only = 0; // -G
    int emit_only = 0;           // --emit-only
    int skip_preprocess = 0;   // -X
    int skip_stdlib = 0;       // -S
    int output_json = 0;       // -j (general "emit JSON" flag)
    int output_ffi_decls = 0;  // --ffi-decls
#ifdef CCCC_HAS_CURL
    char *url_cache_dir = NULL; // --url-cache-dir
    int url_cache_clear = 0;    // --url-cache-clear
#endif
    CCCCAttrTarget attr_target = CCCC_ATTR_TARGET_AUTO; // --attr-target
    int compile_only = 0;      // -c (set whenever -c/--compile is given; semantics:
                                //   "compile, do not execute". -c=bytecode writes bytecode,
                                //   -c=native hands off to the system compiler.)
    int max_errors = 20;        // --max-errors (default: 20)
    int warnings_as_errors = 0; // -Werror / --Werror
    uint64_t warnings = 0;
    uint64_t warning_errors = 0;
    uint64_t warning_no_errors = 0;
    uint64_t warning_sticky_errors = 0; // bits pinned by -Werror=<name>; resist -Wno-error=<name>
    size_t embed_limit = 0;     // --embed-limit (0 = use default)
    int embed_hard_error = 0;   // --embed-hard-limit
    int macro_recursion_limit = -1; // --macro-recursion-limit
    int opt_level = 0; // -O0/-O1/-O2/-O3/-O4 (default: 0 = no optimization)
    int fuse_ops = 0;          // --fuse-ops
    int ffp_contract_fma = 0;  // --fma
    int inline_node_limit = 20; // --inline-limit (default 20, 0=disable)
    int asm_passthru = 0;       // --asm-passthru
    const char *std_arg = NULL; // --std=<standard>
    const char **ffi_allow_args = NULL;
    int ffi_allow_args_count = 0;
    const char **ffi_deny_args = NULL;
    int ffi_deny_args_count = 0;
    int disable_all_ffi = 0;
    int ffi_errors_fatal = 0;
    int enable_ffi_type_checking = 0;
    int vm_profile = 0;
    int vm_profile_text = 0;
    const char *vm_profile_mode = NULL;
    const char *vm_profile_input = NULL;
    int vm_profile_ran = 0;
    const char *entry_name = NULL; // -e / --entry
    enum { COMPILE_NONE, COMPILE_BYTECODE, COMPILE_NATIVE } compile_format = COMPILE_NONE;
    int comptime_include_all = 0; // --comptime-include-all
    int allow_comptime_pp_bleed = 0; // --allow-comptime-pp-bleed
    int run_ngrams = 0;            // 0 = off; 2 or 3 = enabled with n-gram size
    int ngrams_top = 25;
    int ngrams_per_file = 0;
    int run_fusion = 0;            // 0 = off; >0 = enabled, value is top-N
    CcNgramState *ngram_state = NULL;
    CcFusionState *fusion_state = NULL;
    int testing_mode = 0;          // --testing
    const char *test_glob = NULL;  // --test=GLOB
    const char *suite_filter = NULL; // --test-suite=NAME
    int list_tests = 0;            // --list-tests
    int fail_fast = 0;             // --fail-fast
    int test_timeout = 0;          // --test-timeout=N
    CcTestFormat test_format = TEST_FORMAT_TAP; // --test-format=FORMAT
    int build_mode = 0;            // --build
    const char *build_entry = NULL;   // --build-entry=NAME
    const char *build_target = NULL;  // --build-target=NAME
    const char *build_out_dir = NULL; // --build-out-dir=PATH (default "build")
    int build_dry_run = 0;         // --build-dry-run
    int build_verbose = 0;         // --build-verbose
    int build_quiet = 0;           // --build-quiet
    int build_keep_going = 0;      // --build-keep-going
    int build_jobs = 1;            // --build-jobs=N
    int build_list_targets = 0;    // --build-list-targets (#540)
    const char *build_profile = NULL; // --build-profile=NAME (#548)
    const char *build_triple = NULL;  // --build-triple=TRIPLE (#547)
    const char *build_cc = NULL;      // --build-cc=COMPILER (#547)
    const char *build_cache = NULL;   // --build-cache[=PATH] (#546)
    const char **build_tool_allow = NULL; // --build-tool-allow=name,...
    int build_tool_allow_count = 0;
    const char **build_options = NULL;    // --build-option=key=value (#559)
    int build_options_count = 0;
    int build_install = 0;                // --build-install (#560)
    const char **link_paths = NULL;       // --link lib.c4a (#565)
    int link_paths_count = 0;

    if (argc <= 1)
        usage(argv[0], 1);

    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"out", required_argument, 0, 'o'},
        {"disassemble", no_argument, 0, 'd'},
        {"verbose", no_argument, 0, 'v'},
        {"ast", no_argument, 0, 'a'},
        {"print-tokens", no_argument, 0, 'p'},
        {"preprocess", no_argument, 0, 'E'},
        {"dump-expanded", no_argument, 0, 'm'},
        {"emit-generated", no_argument, 0, 'G'},
        {"no-preprocess", no_argument, 0, 'X'},
        {"no-stdlib", no_argument, 0, 'S'},
        {"json", no_argument, 0, 'j'},
        {"ffi-decls", no_argument, 0, 'f'},
        {"compile", optional_argument, 0, 'c'},
        {"debug", no_argument, 0, 'g'},
        {"safety", required_argument, 0, 1012},
        {"bounds-checks", no_argument, 0, 'B'},
        {"uaf-detection", no_argument, 0, 1078},
        {"type-checks", no_argument, 0, 1079},
        {"uninitialized-detection", no_argument, 0, 1038},
        {"overflow-checks", no_argument, 0, 1034},
        {"stack-canaries", no_argument, 0, 1039},
        {"heap-canaries", no_argument, 0, 1080},
        {"pointer-sanitizer", no_argument, 0, 'P'},
        {"memory-leak-detection", no_argument, 0, 'M'},
        {"stack-instrumentation", no_argument, 0, 1043},
        {"stack-errors", no_argument, 0, 1005},
        {"dangling-pointers", no_argument, 0, 1001},
        {"alignment-checks", no_argument, 0, 1002},
        {"provenance-tracking", no_argument, 0, 1003},
        {"invalid-arithmetic", no_argument, 0, 1004},
        {"format-string-checks", no_argument, 0, 1044},
        {"random-canaries", no_argument, 0, 1081},
        {"memory-poisoning", no_argument, 0, 1007},
        {"memory-tagging", no_argument, 0, 1045},
        {"vm-heap", no_argument, 0, 'V'},
        {"control-flow-integrity", no_argument, 0, 'C'},
        {"thread-safety", no_argument, 0, 'T'},
        {"include", required_argument, 0, 'I'},
        {"isystem", required_argument, 0, 'i'},
        {"library-path", required_argument, 0, 'L'},
        {"library", required_argument, 0, 'l'},
        {"define", required_argument, 0, 'D'},
        {"undef", required_argument, 0, 'U'},
        {"url-cache-dir", required_argument, 0, 1008},
        {"url-cache-clear", no_argument, 0, 1009},
        {"max-errors", required_argument, 0, 'n'},
        {"Werror", no_argument, 0, 'w'},
        {"embed-limit", required_argument, 0, 1048},
        {"embed-hard-limit", no_argument, 0, 1060},
        {"optimize", optional_argument, 0, 'O'},
        {"fuse-ops", no_argument, 0, 1070},
        {"fma", no_argument, 0, 1072},
        {"macro-recursion-limit", required_argument, 0, 'r'},
        {"std", required_argument, 0, 's'},
        {"ffi-allow", required_argument, 0, 1052},
        {"ffi-deny", required_argument, 0, 1053},
        {"disable-ffi", no_argument, 0, 'F'},
        {"ffi-errors-fatal", no_argument, 0, 1055},
        {"ffi-type-checking", no_argument, 0, 1024},
        {"vm-profile", no_argument, 0, 1056},
        {"entry", required_argument, 0, 'e'},
        {"ngrams", optional_argument, 0, 1057},
        {"ngrams-top", required_argument, 0, 1030},
        {"ngrams-per-file", no_argument, 0, 1031},
        {"fusion-candidates", optional_argument, 0, 1058},
        {"comptime-include-all", no_argument, 0, 1050},
        {"allow-comptime-pp-bleed", no_argument, 0, 1068},
        {"inline-limit", required_argument, 0, 1051},
        {"asm-passthru", no_argument, 0, 'A'},
        {"testing", no_argument, 0, 't'},
        {"test", required_argument, 0, 1061},
        {"test-suite", required_argument, 0, 1062},
        {"list-tests", no_argument, 0, 1063},
        {"fail-fast", no_argument, 0, 1064},
        {"test-timeout", required_argument, 0, 1065},
        {"test-format", required_argument, 0, 1066},
        {"emit-only", no_argument, 0, 1067},
        {"attr-target", required_argument, 0, 1069},
        {"no-debug-on-crash", no_argument, 0, 1071},
        {"build", no_argument, 0, 'b'},
        {"build-entry", required_argument, 0, 1074},
        {"build-out-dir", required_argument, 0, 1075},
        {"build-dry-run", no_argument, 0, 1076},
        {"build-target", required_argument, 0, 1077},
        {"build-tool-allow", required_argument, 0, 1082},
        {"build-jobs",       required_argument, 0, 1083},
        {"build-keep-going", no_argument,       0, 1084},
        {"build-quiet",      no_argument,       0, 1085},
        {"build-verbose",    no_argument,       0, 1086},
        {"build-list-targets", no_argument,    0, 1087},
        {"build-profile",    required_argument, 0, 1088},
        {"build-triple",     required_argument, 0, 1089},
        {"build-cc",         required_argument, 0, 1090},
        {"build-cache",      optional_argument, 0, 1091},
        {"build-option",     required_argument, 0, 1092},
        {"build-install",    no_argument,       0, 1093},
        {"link",             required_argument, 0, 1094},
        {0, 0, 0, 0}};

    // Find "--" separator: args after it are forwarded to the compiled program
    int dashdash = -1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) { dashdash = i; break; }
    }
    int getopt_argc = (dashdash >= 0) ? dashdash : argc;

    const char *optstring = "0123haI:L:D:U:o:c::dvgiPEMGXSjVCl:W:e:O::FbTmptn:r:s:ABfw";
    int opt;
    opterr = 0; // we'll handle errors explicitly
    while ((opt = getopt_long(getopt_argc, (char *const *)argv, optstring,
                              long_options, NULL)) != -1) {
        switch (opt) {
        case 'h':
            usage(argv[0], 0);
            break;
        case '0':
            // Safety level 0: None - explicitly clear all safety flags
            flags = 0;
            cli_flags_mask |= CCCC_SAFETY_PRESET_BITS;
            break;
        case '1':
            // Safety level 1: Basic - essential low-overhead checks
            flags |= CCCC_SAFETY_BASIC;
            cli_flags_mask |= CCCC_SAFETY_PRESET_BITS;
            break;
        case '2':
            // Safety level 2: Standard - comprehensive development safety
            flags |= CCCC_SAFETY_STANDARD;
            cli_flags_mask |= CCCC_SAFETY_PRESET_BITS;
            break;
        case '3':
            // Safety level 3: Maximum - all safety features
            flags |= CCCC_SAFETY_MAX;
            cli_flags_mask |= CCCC_SAFETY_PRESET_BITS;
            break;
        case 1012:
            // --safety=<level> flag
            if (strncmp(optarg, "none", sizeof("none")) == 0 ||
                strncmp(optarg, "0", sizeof("0")) == 0) {
                flags = 0;
            } else if (strncmp(optarg, "basic", sizeof("basic")) == 0 ||
                       strncmp(optarg, "1", sizeof("1")) == 0) {
                flags |= CCCC_SAFETY_BASIC;
            } else if (strncmp(optarg, "standard", sizeof("standard")) == 0 ||
                       strncmp(optarg, "2", sizeof("2")) == 0) {
                flags |= CCCC_SAFETY_STANDARD;
            } else if (strncmp(optarg, "max", sizeof("max")) == 0 ||
                       strncmp(optarg, "3", sizeof("3")) == 0) {
                flags |= CCCC_SAFETY_MAX;
            } else {
                fprintf(stderr,
                        "error: invalid safety level '%s' (use "
                        "none/basic/standard/max or 0/1/2/3)\n",
                        optarg);
                usage(argv[0], 1);
            }
            cli_flags_mask |= CCCC_SAFETY_PRESET_BITS;
            break;
        case 'o':
            if (out_file) {
                fprintf(stderr, "error: only one -o/--out allowed\n");
                usage(argv[0], 1);
            }
            out_file = strdup(optarg);
            break;
        case 'e':
            entry_name = optarg;
            break;
        case 'd':
            disassemble = 1;
            break;
        case 'v':
            verbose = 1;
            break;
        case 'a':
            dump_ast = 1;
            break;
        case 'g':
            flags |= CCCC_ENABLE_DEBUGGER;
            break;
        case 'I':
            inc_paths =
                realloc(inc_paths, sizeof(*inc_paths) * (inc_paths_count + 1));
            inc_paths[inc_paths_count++] = strdup(optarg);
            break;
        case 'L':
            lib_paths =
                realloc(lib_paths, sizeof(*lib_paths) * (lib_paths_count + 1));
            lib_paths[lib_paths_count++] = strdup(optarg);
            break;
        case 'i': // --isystem
            sys_inc_paths =
                realloc(sys_inc_paths,
                        sizeof(*sys_inc_paths) * (sys_inc_paths_count + 1));
            sys_inc_paths[sys_inc_paths_count++] = strdup(optarg);
            break;
        case 'l': // --library
            libs = realloc(libs, sizeof(*libs) * (libs_count + 1));
            libs[libs_count++] = strdup(optarg);
            break;
        case 'D':
            defines = realloc(defines, sizeof(*defines) * (defines_count + 1));
            defines[defines_count++] = strdup(optarg);
            break;
        case 'U':
            undefs = realloc(undefs, sizeof(*undefs) * (undefs_count + 1));
            undefs[undefs_count++] = strdup(optarg);
            break;
        case 'B': // --bounds-checks
            flags |= CCCC_BOUNDS_CHECKS;
            cli_flags_mask |= CCCC_BOUNDS_CHECKS;
            break;
        case 1078: // --uaf-detection
            flags |= CCCC_UAF_DETECTION;
            cli_flags_mask |= CCCC_UAF_DETECTION;
            break;
        case 1079: // --type-checks
            flags |= CCCC_TYPE_CHECKS;
            cli_flags_mask |= CCCC_TYPE_CHECKS;
            break;
        case 1038: // --uninitialized-detection
            flags |= CCCC_UNINIT_DETECTION;
            break;
        case 1034: // --overflow-checks
            flags |= CCCC_OVERFLOW_CHECKS;
            cli_flags_mask |= CCCC_OVERFLOW_CHECKS;
            break;

        case 1039: // --stack-canaries
            flags |= CCCC_STACK_CANARIES;
            cli_flags_mask |= CCCC_STACK_CANARIES;
            break;
        case 1080: // --heap-canaries
            flags |= CCCC_HEAP_CANARIES;
            cli_flags_mask |= CCCC_HEAP_CANARIES;
            break;
        case 'P': // --pointer-sanitizer
            flags |= CCCC_POINTER_SANITIZER;
            cli_flags_mask |= CCCC_POINTER_SANITIZER;
            break;
        case 'M': // --memory-leak-detection
            flags |= CCCC_MEMORY_LEAK_DETECT;
            cli_flags_mask |= CCCC_MEMORY_LEAK_DETECT;
            break;
        case 1043: // --stack-instrumentation
            flags |= CCCC_STACK_INSTR;
            break;
        case 1001:
            flags |= CCCC_DANGLING_DETECT;
            break;
        case 1002:
            flags |= CCCC_ALIGNMENT_CHECKS;
            break;
        case 1003:
            flags |= CCCC_PROVENANCE_TRACK;
            break;
        case 1004:
            flags |= CCCC_INVALID_ARITH;
            break;
        case 1005:
            flags |= CCCC_STACK_INSTR_ERRORS;
            break;
        case 1044: // --format-string-checks
            flags |= CCCC_FORMAT_STR_CHECKS;
            warnings |= CCCC_WARN_FORMAT;
            break;
        case 1081: // --random-canaries
            flags |= CCCC_RANDOM_CANARIES;
            break;
        case 1007:
            flags |= CCCC_MEMORY_POISONING;
            break;
        case 1045: // --memory-tagging
            flags |= CCCC_MEMORY_TAGGING;
            cli_flags_mask |= CCCC_MEMORY_TAGGING;
            break;
        case 'T': // --thread-safety
            flags |= CCCC_THREAD_SAFETY;
            break;
        case 'V':
            flags |= CCCC_VM_HEAP;
            break;
        case 'C':
            flags |= CCCC_CFI;
            break;
        case 'p':
            print_tokens = 1;
            break;
        case 'E':
            preprocess_only = 1;
            break;
        case 'm':
            dump_expanded_only = 1;
            break;
        case 'G':
            emit_generated_only = 1;
            dump_expanded_only = 1; // -G implies serialization mode
            break;
        case 'X':
            skip_preprocess = 1;
            break;
        case 'S':
            skip_stdlib = 1;
            break;
        case 'j':
            output_json = 1;
            break;
        case 'c': { // -c[FMT]/--compile[=FMT]
            // Bare -c / --compile defaults to bytecode. The optional
            // argument selects the format. Note: GNU getopt's `::` only
            // supports attached form for short options (e.g. `-cnative`),
            // not `-c=native`. The long form accepts `--compile=native`
            // and `--compile native` (the latter as a separate arg). Strip
            // a leading `=` to be friendly to BSD getopt / `-c=native`
            // callers even though GNU's getopt rejects that form outright.
            compile_only = 1;
            const char *fmt = optarg;
            if (fmt && fmt[0] == '=')
                fmt++;
            if (!fmt || !*fmt) {
                compile_format = COMPILE_BYTECODE;
            } else if (strcmp(fmt, "bytecode") == 0 || strcmp(fmt, "bc") == 0 ||
                       strcmp(fmt, "c4") == 0) {
                compile_format = COMPILE_BYTECODE;
            } else if (strcmp(fmt, "native") == 0 || strcmp(fmt, "n") == 0) {
                compile_format = COMPILE_NATIVE;
            } else {
                fprintf(stderr,
                        "error: invalid --compile format '%s' "
                        "(use 'bytecode' or 'native')\n",
                        fmt);
                usage(argv[0], 1);
            }
            break;
        }
#ifdef CCCC_HAS_CURL
        case 1008:
            url_cache_dir = strdup(optarg);
            break;
        case 1009:
            url_cache_clear = 1;
            break;
#endif
        case 'n': // --max-errors
            max_errors = atoi(optarg);
            if (max_errors <= 0) {
                fprintf(stderr,
                        "error: --max-errors must be a positive integer\n");
                usage(argv[0], 1);
            }
            break;
        case 'w': // --Werror
            warnings_as_errors = 1;
            warning_no_errors = 0;
            break;
        case 'W':
            parse_warning_option(optarg, &warnings, &warning_errors,
                                 &warning_no_errors, &warning_sticky_errors,
                                 &warnings_as_errors);
            break;
        case 1048: // --embed-limit
            embed_limit = parse_size(optarg, "--embed-limit");
            break;
        case 1060: // --embed-hard-limit
            embed_hard_error = 1;
            break;
        case 'O': // --optimize / -O (also matches --optimize via long_options)
            if (optarg == NULL) {
                // Just -O or --optimize without argument means -O1
                opt_level = 1;
            } else if (optarg[0] >= '0' && optarg[0] <= '4' &&
                       optarg[1] == '\0') {
                opt_level = optarg[0] - '0';
                if (opt_level >= 4)
                    fuse_ops = 1;
            } else {
                fprintf(stderr,
                        "error: invalid optimization level '%s' (use 0, 1, 2, "
                        "3, or 4)\n",
                        optarg);
                usage(argv[0], 1);
            }
            cli_opt_level_set = true;
            break;
        case 1070:
            fuse_ops = 1;
            break;
        case 1072:
            fuse_ops = 1;
            ffp_contract_fma = 1;
            break;
        case 'r': { // --macro-recursion-limit
            char *end = NULL;
            long val = strtol(optarg, &end, 10);
            if (!optarg[0] || *end != '\0' || val < 0 || val > INT32_MAX) {
                fprintf(stderr,
                        "error: --macro-recursion-limit must be a "
                        "non-negative integer\n");
                usage(argv[0], 1);
            }
            macro_recursion_limit = (int)val;
            break;
        }
        case 's': // --std=<standard>
            std_arg = optarg;
            break;
        case 1052: // --ffi-allow
            ffi_allow_args = realloc(ffi_allow_args, sizeof(*ffi_allow_args) *
                                                         (ffi_allow_args_count + 1));
            ffi_allow_args[ffi_allow_args_count++] = strdup(optarg);
            break;
        case 1053: // --ffi-deny
            ffi_deny_args = realloc(ffi_deny_args, sizeof(*ffi_deny_args) *
                                                     (ffi_deny_args_count + 1));
            ffi_deny_args[ffi_deny_args_count++] = strdup(optarg);
            break;
        case 'F': // --disable-ffi
            disable_all_ffi = 1;
            break;
        case 1055: // --ffi-errors-fatal
            ffi_errors_fatal = 1;
            break;
        case 1024:
            enable_ffi_type_checking = 1;
            break;
        case 1056: // --vm-profile
            vm_profile = 1;
            vm_profile_text = 1;
            break;
        case 1057: { // --ngrams[=N]
            if (optarg == NULL) {
                run_ngrams = 2;
            } else if (optarg[0] >= '0' && optarg[0] <= '9' && optarg[1] == '\0') {
                run_ngrams = optarg[0] - '0';
            } else {
                fprintf(stderr,
                        "error: invalid --ngrams value '%s' (use 2 or 3)\n",
                        optarg);
                usage(argv[0], 1);
            }
            if (run_ngrams != 2 && run_ngrams != 3) {
                fprintf(stderr,
                        "error: --ngrams must be 2 or 3 (got %d)\n", run_ngrams);
                usage(argv[0], 1);
            }
            break;
        }
        case 1030: { // --ngrams-top=N
            char *end = NULL;
            long val = strtol(optarg, &end, 10);
            if (!optarg[0] || *end != '\0' || val <= 0 || val > INT32_MAX) {
                fprintf(stderr,
                        "error: --ngrams-top must be a positive integer\n");
                usage(argv[0], 1);
            }
            ngrams_top = (int)val;
            break;
        }
        case 1031: // --ngrams-per-file
            ngrams_per_file = 1;
            break;
        case 1050: // --comptime-include-all
            comptime_include_all = 1;
            break;
        case 1051: { // --inline-limit
            char *end = NULL;
            long val = strtol(optarg, &end, 10);
            if (!optarg[0] || *end != '\0' || val < 0 || val > INT32_MAX) {
                fprintf(stderr,
                        "error: --inline-limit must be a non-negative integer\n");
                usage(argv[0], 1);
            }
            inline_node_limit = (int)val;
            break;
        }
        case 'A': // --asm-passthru
            asm_passthru = 1;
            break;
        case 't': // --testing
            testing_mode = 1;
            break;
        case 1061: // --test=GLOB
            test_glob = optarg;
            testing_mode = 1;
            break;
        case 1062: // --test-suite=NAME
            suite_filter = optarg;
            testing_mode = 1;
            break;
        case 1063: // --list-tests
            list_tests = 1;
            testing_mode = 1;
            break;
        case 1064: // --fail-fast
            fail_fast = 1;
            testing_mode = 1;
            break;
        case 1065: // --test-timeout=N
            test_timeout = atoi(optarg);
            testing_mode = 1;
            break;
        case 1067: // --emit-only
            emit_only = 1;
            break;
        case 1068: // --allow-comptime-pp-bleed
            allow_comptime_pp_bleed = 1;
            break;
        case 1069: // --attr-target=auto|c23|gnu|msvc|strip
            if (strcmp(optarg, "auto") == 0) {
                attr_target = CCCC_ATTR_TARGET_AUTO;
            } else if (strcmp(optarg, "c23") == 0) {
                attr_target = CCCC_ATTR_TARGET_C23;
            } else if (strcmp(optarg, "gnu") == 0) {
                attr_target = CCCC_ATTR_TARGET_GNU;
            } else if (strcmp(optarg, "msvc") == 0) {
                attr_target = CCCC_ATTR_TARGET_MSVC;
            } else if (strcmp(optarg, "strip") == 0) {
                attr_target = CCCC_ATTR_TARGET_STRIP;
            } else {
                fprintf(stderr,
                        "error: invalid --attr-target '%s' "
                        "(use 'auto', 'c23', 'gnu', 'msvc', or 'strip')\n",
                        optarg);
                usage(argv[0], 1);
            }
            break;
        case 1071: // --no-debug-on-crash
            flags |= CCCC_NO_DEBUG_ON_CRASH;
            break;
        case 'b': // --build
            build_mode = 1;
            break;
        case 1074: // --build-entry=NAME
            build_entry = optarg;
            build_mode = 1;
            break;
        case 1075: // --build-out-dir=PATH
            build_out_dir = optarg;
            build_mode = 1;
            break;
        case 1076: // --build-dry-run
            build_dry_run = 1;
            build_mode = 1;
            break;
        case 1077: // --build-target=NAME
            build_target = optarg;
            build_mode = 1;
            break;
        case 1082: { // --build-tool-allow=name[,name,...]
            // Accept comma-separated names: --build-tool-allow=cc,ar,pkg-config
            // or repeated flags: --build-tool-allow=cc --build-tool-allow=ar
            char *tmp = strdup(optarg);
            char *saveptr = NULL;
            char *tok = strtok_r(tmp, ",", &saveptr);
            while (tok) {
                build_tool_allow = realloc(build_tool_allow,
                                           sizeof(*build_tool_allow) *
                                           (build_tool_allow_count + 1));
                build_tool_allow[build_tool_allow_count++] = strdup(tok);
                tok = strtok_r(NULL, ",", &saveptr);
            }
            free(tmp);
            build_mode = 1;
            break;
        }
        case 1083: { // --build-jobs=N
            int n = atoi(optarg);
            if (n < 1) {
                fprintf(stderr, "error: --build-jobs requires a positive integer\n");
                usage(argv[0], 1);
            }
            build_jobs = n;
            build_mode = 1;
            break;
        }
        case 1084: // --build-keep-going
            build_keep_going = 1;
            build_mode = 1;
            break;
        case 1085: // --build-quiet
            build_quiet = 1;
            build_mode = 1;
            break;
        case 1086: // --build-verbose
            build_verbose = 1;
            build_mode = 1;
            break;
        case 1087: // --build-list-targets
            build_list_targets = 1;
            build_mode = 1;
            break;
        case 1088: // --build-profile=NAME
            build_profile = optarg;
            build_mode = 1;
            break;
        case 1089: // --build-triple=TRIPLE
            build_triple = optarg;
            build_mode = 1;
            break;
        case 1090: // --build-cc=COMPILER
            build_cc = optarg;
            build_mode = 1;
            break;
        case 1091: // --build-cache[=PATH]
            build_cache = optarg ? optarg : "";
            build_mode = 1;
            break;
        case 1092: { // --build-option=key[=value]
            const char **tmp = realloc(build_options,
                                       (build_options_count + 1) * sizeof(*build_options));
            if (!tmp) { fprintf(stderr, "error: out of memory\n"); return 1; }
            build_options = tmp;
            build_options[build_options_count++] = optarg;
            build_mode = 1;
            break;
        }
        case 1093: // --build-install
            build_install = 1;
            build_mode = 1;
            break;
        case 1094: { // --link lib.c4a (#565)
            void *tmp = realloc(link_paths,
                                (size_t)(link_paths_count + 1) * sizeof(*link_paths));
            if (!tmp) { fprintf(stderr, "error: out of memory\n"); return 1; }
            link_paths = tmp;
            link_paths[link_paths_count++] = optarg;
            break;
        }
        case 1066: // --test-format=FORMAT
            if (strcmp(optarg, "tap") == 0) {
                test_format = TEST_FORMAT_TAP;
            } else if (strcmp(optarg, "plain") == 0) {
                test_format = TEST_FORMAT_PLAIN;
            } else if (strcmp(optarg, "json") == 0) {
                test_format = TEST_FORMAT_JSON;
            } else {
                fprintf(stderr, "error: invalid --test-format '%s' "
                        "(use 'tap', 'plain', or 'json')\n", optarg);
                usage(argv[0], 1);
            }
            testing_mode = 1;
            break;
        case 1058: { // --fusion-candidates[=N]
            if (optarg == NULL) {
                run_fusion = 1;
            } else {
                char *end = NULL;
                long val = strtol(optarg, &end, 10);
                if (!optarg[0] || *end != '\0' || val <= 0 || val > INT32_MAX) {
                    fprintf(stderr,
                            "error: --fusion-candidates top-N must be a "
                            "positive integer\n");
                    usage(argv[0], 1);
                }
                run_fusion = (int)val;
            }
            break;
        }
        case 'f': // --ffi-decls
            output_ffi_decls = 1;
            break;
        case '?':
            if (optopt)
                fprintf(stderr, "error: option -%c requires an argument\n",
                        optopt);
            else if (optind > 0 && argv[optind - 1] &&
                     argv[optind - 1][0] == '-')
                fprintf(stderr, "error: unknown option %s\n", argv[optind - 1]);
            else
                fprintf(stderr, "error: unknown parsing error\n");
            usage(argv[0], 1);
            break;
        default:
            usage(argv[0], 1);
        }
    }

    /* Remaining arguments are input files (positional, up to "--" if present) */
    for (int i = optind; i < getopt_argc; i++) {
        const char *a = argv[i];
        if (strncmp(a, "-", sizeof("-")) == 0) {
            input_files = realloc(input_files, sizeof(*input_files) *
                                                   (input_files_count + 1));
            input_files[input_files_count++] = strdup("-");
        } else {
            input_files = realloc(input_files, sizeof(*input_files) *
                                                   (input_files_count + 1));
            input_files[input_files_count++] = strdup(a);
        }
    }
    // If no input files, error
    if (input_files_count == 0) {
        fprintf(stderr, "error: no input files\n");
        usage((char *)argv[0], 1);
    }

    if (build_mode) {
        // --build runs the build script in the VM; the host runner compiles the
        // declared targets. VM-only and output modes do not apply here.
        if (compile_format != COMPILE_NONE || disassemble || opt_level != 0 ||
            fuse_ops || vm_profile || out_file || preprocess_only ||
            dump_expanded_only || print_tokens || output_json ||
            output_ffi_decls || dump_ast) {
            fprintf(stderr,
                    "error: --build cannot be combined with VM/output options "
                    "(-c, -d, -O<n>, --vm-profile, -o, -E, -M, --ast, ...)\n");
            usage(argv[0], 1);
        }
        if (flags & CCCC_ENABLE_DEBUGGER) {
            fprintf(stderr, "error: --build cannot be combined with --debug\n");
            usage(argv[0], 1);
        }
    }

    if (compile_format == COMPILE_NATIVE) {
        if (preprocess_only || dump_expanded_only || print_tokens ||
            output_json || output_ffi_decls || dump_ast) {
            fprintf(stderr,
                    "error: -c=native cannot be combined with frontend output modes\n");
            usage(argv[0], 1);
        }
        if (disassemble || entry_name || opt_level != 0 || fuse_ops ||
            vm_profile) {
            fprintf(stderr,
                    "error: -c=native cannot be combined with VM bytecode options\n");
            usage(argv[0], 1);
        }
        if (flags != 0) {
            fprintf(stderr,
                    "error: -c=native cannot be combined with VM runtime safety/debug options\n");
            usage(argv[0], 1);
        }
        if (ffi_allow_args_count || ffi_deny_args_count || disable_all_ffi ||
            ffi_errors_fatal || enable_ffi_type_checking) {
            fprintf(stderr,
                    "error: -c=native cannot be combined with CCCC FFI policy options\n");
            usage(argv[0], 1);
        }
        for (int i = 0; i < input_files_count; i++) {
            size_t len = strlen(input_files[i]);
            if (len > 3 &&
                strncmp(input_files[i] + len - 3, ".c4", sizeof(".c4")) == 0) {
                fprintf(stderr,
                        "error: -c=native expects C source input, not bytecode '%s'\n",
                        input_files[i]);
                usage(argv[0], 1);
            }
        }
        if (!out_file && !testing_mode) {
            fprintf(stderr,
                    "error: -c=native requires -o <file>\n");
            usage(argv[0], 1);
        }
    }

    if (run_ngrams || run_fusion) {
        // Static analysis is mutually exclusive with execution / output modes.
        if (run_ngrams && run_fusion) {
            fprintf(stderr,
                    "error: --ngrams and --fusion-candidates cannot be combined\n");
            usage(argv[0], 1);
        }
        if (preprocess_only || dump_expanded_only || print_tokens ||
            output_json || output_ffi_decls || dump_ast) {
            fprintf(stderr,
                    "error: --ngrams/--fusion-candidates cannot be combined "
                    "with frontend output modes\n");
            usage(argv[0], 1);
        }
        if (compile_only || disassemble || out_file || entry_name) {
            fprintf(stderr,
                    "error: --ngrams/--fusion-candidates cannot be combined "
                    "with VM bytecode output or entry options\n");
            usage(argv[0], 1);
        }
        if (vm_profile) {
            fprintf(stderr,
                    "error: --ngrams/--fusion-candidates cannot be combined "
                    "with --vm-profile\n");
            usage(argv[0], 1);
        }
        if (flags & CCCC_ENABLE_DEBUGGER) {
            fprintf(stderr,
                    "error: --ngrams/--fusion-candidates cannot be combined "
                    "with -g/--debug\n");
            usage(argv[0], 1);
        }
        if (flags != 0) {
            fprintf(stderr,
                    "error: --ngrams/--fusion-candidates cannot be combined "
                    "with VM runtime safety options\n");
            usage(argv[0], 1);
        }
        if (compile_format == COMPILE_NATIVE) {
            fprintf(stderr,
                    "error: --ngrams/--fusion-candidates cannot be combined "
                    "with -c=native\n");
            usage(argv[0], 1);
        }
    }

    // Auto-enable the interactive debugger when running interactively, so a
    // fatal runtime error traps into the debugger instead of just exiting
    // (ticket #405). Excluded for --testing/--test/--test-suite since those
    // fork child processes (src/tests.c) that inherit the parent's TTY, and
    // an external test harness is expected to pass --no-debug-on-crash
    // itself if it invokes cccc directly.
    bool auto_debug_on_crash =
        !(flags & CCCC_ENABLE_DEBUGGER) && !(flags & CCCC_NO_DEBUG_ON_CRASH) &&
        !testing_mode && !build_mode &&
        CCCC_ISATTY(CCCC_FILENO(stdin)) && CCCC_ISATTY(CCCC_FILENO(stdout));
    if (auto_debug_on_crash)
        flags |= CCCC_ENABLE_DEBUGGER;

    VirtualMachine vm;
    cc_init(&vm, flags);
    if (auto_debug_on_crash)
        vm.dbg.crash_debug_auto = true;
    vm.compiler.cli_flags_mask = cli_flags_mask;
    vm.compiler.cli_opt_level_set = cli_opt_level_set;
    vm.compiler.native_mode = (compile_format == COMPILE_NATIVE);
    vm.compiler.compile_only = compile_only;
    vm.compiler.deferred_link = (link_paths_count > 0 && !compile_only);
    vm.compiler.asm_passthru = asm_passthru;
    vm.compiler.comptime_include_all = comptime_include_all;
    vm.compiler.allow_comptime_pp_bleed = allow_comptime_pp_bleed;
    vm.compiler.emit_strict = emit_only;
    vm.compiler.attr_target = attr_target;
    vm.compiler.entry_name = (char *)entry_name;
    vm.compiler.testing_mode = (bool)testing_mode;
    vm.compiler.build_mode = (bool)build_mode;
    vm.compiler.diagnostic_json = output_json;
    vm.disable_all_ffi = disable_all_ffi;
    vm.ffi_errors_fatal = ffi_errors_fatal;
    vm.enable_ffi_type_checking = enable_ffi_type_checking;
    vm.vm_profile_enabled = vm_profile;
    if (vm_profile) {
        vm.vm_profile_trigram_counts = calloc(
            (size_t)OP_COUNT * OP_COUNT * OP_COUNT, sizeof(uint64_t));
        // Failure is non-fatal: trigram section will be skipped in JSON output
    }
    for (int i = 0; i < ffi_allow_args_count; i++)
        configure_ffi_name_list(&vm, ffi_allow_args[i], cc_ffi_allow);
    for (int i = 0; i < ffi_deny_args_count; i++)
        configure_ffi_name_list(&vm, ffi_deny_args[i], cc_ffi_deny);

    if (verbose)
        vm.debug_vm = 1;

    // If the only input file is "-", read stdin into a temporary file and
    // replace it
    if (input_files_count == 1 &&
        strncmp(input_files[0], "-", sizeof("-")) == 0) {
        char *tmp = read_stdin_to_tmp();
        if (!tmp) {
            fprintf(stderr,
                    "error: failed to read stdin into temporary file\n");
            exit_code = 1;
            goto BAIL;
        }
        free((void *)input_files[0]);
        input_files[0] = tmp;
    }

    // Check if input is a bytecode file (.c4 extension)
    // If so, load and run it directly without compilation
    //
    // In analysis mode we accept multiple .c4 files and walk each one in
    // turn, aggregating counts across all of them.
    if (input_files_count >= 1 && (run_ngrams || run_fusion)) {
        int all_c4 = 1;
        for (int i = 0; i < input_files_count; i++) {
            size_t len = strlen(input_files[i]);
            if (len <= 3 ||
                strncmp(input_files[i] + len - 3, ".c4", sizeof(".c4")) != 0) {
                all_c4 = 0;
                break;
            }
        }
        if (all_c4) {
            if (run_ngrams) {
                CcAnalyzeNgramOptions opts = {
                    .n = run_ngrams,
                    .top_n = ngrams_top,
                    .per_file = ngrams_per_file,
                };
                ngram_state = cc_analyze_ngram_begin(&opts);
                for (int i = 0; i < input_files_count; i++) {
                    if (cc_load_bytecode(&vm, input_files[i]) != 0) {
                        fprintf(stderr,
                                "error: failed to load bytecode from %s\n",
                                input_files[i]);
                        exit_code = 1;
                        goto BAIL;
                    }
                    cc_analyze_ngram_feed(ngram_state, vm.text_seg,
                                          (long long)vm.text_ptr + 1,
                                          input_files[i], stdout);
                }
                cc_analyze_ngram_finish(ngram_state, stdout);
                ngram_state = NULL;
            } else {
                CcAnalyzeFusionOptions opts = {
                    .top_n = run_fusion,
                    .json = output_json,
                };
                fusion_state = cc_analyze_fusion_begin(&opts);
                for (int i = 0; i < input_files_count; i++) {
                    if (cc_load_bytecode(&vm, input_files[i]) != 0) {
                        fprintf(stderr,
                                "error: failed to load bytecode from %s\n",
                                input_files[i]);
                        exit_code = 1;
                        goto BAIL;
                    }
                    cc_analyze_fusion_feed(fusion_state, vm.text_seg,
                                           (long long)vm.text_ptr + 1,
                                           input_files[i], stdout);
                }
                cc_analyze_fusion_finish(fusion_state, stdout);
                fusion_state = NULL;
            }
            goto BAIL;
        }
    }

    if (input_files_count == 1) {
        const char *input_file = input_files[0];
        size_t len = strlen(input_file);
        if (len > 3 &&
            strncmp(input_file + len - 3, ".c4", sizeof(".c4")) == 0) {
            // Load bytecode file
            if (cc_load_bytecode(&vm, input_file) != 0) {
                fprintf(stderr, "error: failed to load bytecode from %s\n",
                        input_file);
                exit_code = 1;
                goto BAIL;
            }

            if (disassemble) {
                cc_disassemble(&vm);
                goto BAIL;
            }

            // Rehydrate stdlib FFI entries. This mirrors parse+execute so
            // wrapper registrations such as realloc and wide-char helpers keep
            // their VM-facing signatures after a bytecode load.
            int stdlib_entries = 0;
            for (int i = 0; i < vm.compiler.ffi_count; i++) {
                if (!vm.compiler.ffi_table[i].is_dynamic_placeholder)
                    stdlib_entries++;
            }
            if (stdlib_entries > 0)
                cc_load_stdlib(&vm);

            if (load_requested_libraries(&vm, libs, libs_count, lib_paths,
                                         lib_paths_count) != 0) {
                exit_code = 1;
                goto BAIL;
            }
            if (cc_rehydrate_asm_passthru(&vm) != 0) {
                exit_code = 1;
                goto BAIL;
            }
            if (verify_dynamic_externs(&vm) != 0) {
                exit_code = 1;
                goto BAIL;
            }

            // Run the loaded bytecode. The loaded program sees its own .c4
            // path as argv[0], plus explicit args after "--" if present.
            int prog_argc = 0;
            char **prog_argv =
                build_c4_argv(&prog_argc, input_file, argc, argv, dashdash);
            exit_code = cc_run(&vm, prog_argc, prog_argv);
            vm_profile_mode = "c4";
            vm_profile_input = input_file;
            vm_profile_ran = 1;
            free(prog_argv);
            goto BAIL;
        }
    }

    // Configure #embed limits if specified
    if (embed_limit > 0) {
        vm.compiler.embed_limit = embed_limit;
        vm.compiler.embed_hard_limit =
            embed_limit; // Use same value for both warnings
    }
    if (embed_hard_error) {
        vm.compiler.embed_hard_error = true;
    }

    // Set optimization level
    vm.compiler.opt_level = opt_level;
    vm.compiler.fuse_ops = fuse_ops;
    vm.compiler.ffp_contract_fma = ffp_contract_fma;
    vm.compiler.inline_node_limit = inline_node_limit;
    if (macro_recursion_limit >= 0)
        vm.compiler.macro_recursion_limit = macro_recursion_limit;

    // Apply --std=<standard> if specified, then re-emit std macros
    if (std_arg) {
        CStdVersion ver = CCCC_STD_C17;
        bool is_gnu = true;
        const char *s = std_arg;
        // Consume optional "gnu" / "c" prefix
        if (strncmp(s, "gnu", 3) == 0) {
            is_gnu = true;
            s += 3;
        } else if (s[0] == 'c') {
            is_gnu = false;
            s += 1;
        } else {
            fprintf(stderr, "error: unknown C standard '%s'\n", std_arg);
            usage(argv[0], 1);
        }
        // Map suffix to version
        if (strcmp(s, "89") == 0 || strcmp(s, "90") == 0) {
            ver = CCCC_STD_C89;
        } else if (strcmp(s, "99") == 0) {
            ver = CCCC_STD_C99;
        } else if (strcmp(s, "11") == 0) {
            ver = CCCC_STD_C11;
        } else if (strcmp(s, "17") == 0 || strcmp(s, "18") == 0) {
            ver = CCCC_STD_C17;
        } else if (strcmp(s, "23") == 0 || strcmp(s, "2x") == 0) {
            ver = CCCC_STD_C23;
        } else {
            fprintf(stderr, "error: unknown C standard '%s'\n", std_arg);
            usage(argv[0], 1);
        }
        vm.compiler.c_std = ver;
        vm.compiler.c_std_gnu = is_gnu;
        define_std_macros(&vm);
    }

    // If random canaries are enabled, regenerate the stack canary
    if (vm.flags & CCCC_RANDOM_CANARIES) {
        vm.stack_canary = generate_random_canary();
    }

    // Enable error collection for better error reporting
#ifdef CCCC_HAS_CURL
    if (url_cache_dir) {
        vm.compiler.url_cache_dir = url_cache_dir;
    }
    if (url_cache_clear) {
        clear_url_cache(&vm);
    }
#endif

    vm.collect_errors = true;
    vm.max_errors = max_errors;
    vm.warnings_as_errors = warnings_as_errors;
    // --thread-safety implicitly enables the discarded-qualifiers warning so
    // _Atomic cast stripping is diagnosed without requiring -Wall.
    if (flags & CCCC_THREAD_SAFETY)
        warnings |= CCCC_WARN_DISCARDED_QUALIFIERS;
    vm.compiler.warnings = warnings;
    vm.compiler.warning_errors = warning_errors;
    vm.compiler.warning_no_errors = warning_no_errors;
    jmp_buf err_buf;
    vm.error_jmp_buf = &err_buf;

    // Set up error handling with setjmp/longjmp
    if (setjmp(err_buf) != 0) {
        // Error occurred during compilation
        cc_print_all_errors(&vm);
        exit_code = vm.dbg.host_fault_signal
                        ? 128 + vm.dbg.host_fault_signal
                        : 1;
        goto BAIL;
    }

    if (!skip_stdlib)
        cc_load_stdlib(&vm);

    // Add CCCC's standard library header directory
    cc_include(&vm, "./include");

    // Add user-specified include paths (these take precedence via search order)
    for (int i = 0; i < inc_paths_count; i++)
        cc_include(&vm, inc_paths[i]);

    // Add system include paths (for non-standard headers with angle brackets)
    for (int i = 0; i < sys_inc_paths_count; i++)
        cc_system_include(&vm, sys_inc_paths[i]);

    for (int i = 0; i < defines_count; i++)
        parse_define(&vm, (char *)defines[i]);
    for (int i = 0; i < undefs_count; i++)
        cc_undef(&vm, (char *)undefs[i]);

    vm.compiler.skip_preprocess = skip_preprocess;

    // In testing mode, inject testing.h before any source is preprocessed so
    // Assert* macros are in scope. Save the returned declaration tokens for
    // prepending to the parse stream after preprocessing.
    Token *test_decls = NULL;
    if (testing_mode)
        test_decls = cc_inject_test_header(&vm);
    else if (build_mode)
        test_decls = cc_inject_build_header(&vm);

    input_tokens = calloc(input_files_count, sizeof(Token *));
    for (int i = 0; i < input_files_count; i++) {
        input_tokens[i] = cc_preprocess(&vm, input_files[i]);
        if (!input_tokens[i]) {
            fprintf(stderr, "error: failed to preprocess %s\n", input_files[i]);
            goto BAIL;
        }
    }

    // Check for errors and warnings after preprocessing
    if (cc_has_errors(&vm) || vm.warning_count > 0) {
        cc_print_all_errors(&vm);
        if (cc_has_errors(&vm)) {
            exit_code = 1;
            goto BAIL;
        }
    }

    // Prepend injected header declarations into the first file's parse stream.
    if ((testing_mode || build_mode) && test_decls && input_files_count > 0) {
        Token *last = test_decls;
        while (last->next && last->next->kind != TK_EOF)
            last = last->next;
        last->next = input_tokens[0];
        input_tokens[0] = test_decls;
    }

    // If -E flag is set, output preprocessed source and exit
    if (preprocess_only) {
        for (int i = 0; i < input_files_count; i++) {
            FILE *f = out_file ? fopen(out_file, "w") : stdout;
            if (!f) {
                fprintf(stderr, "error: failed to open output file %s\n",
                        out_file);
                goto BAIL;
            }

            cc_output_preprocessed(f, &vm, input_tokens[i]);
            if (f != stdout)
                fclose(f);
        }
        goto BAIL;
    }

    // Execute inline (#pragma macro inline) macros before parsing.
    // This compiles and runs them now, synthesises forward declarations for
    // any functions they generate, and prepends those declarations to every
    // input token stream so the parser can resolve calls without manual
    // forward declarations.
    cc_execute_inline_macros(&vm, input_tokens, input_files_count);

    // Register test-runtime FFI symbols after the comptime pass so that any
    // [[cccc::macro]] calling Assert produces an unresolved-symbol error
    // instead of longjmp-ing through an uninitialised jmp_buf (ticket #334).
    if (testing_mode)
        cc_load_test_runtime(&vm);
    if (build_mode)
        cc_load_build_runtime(&vm);

    input_progs = calloc(input_files_count, sizeof(Obj *));
    for (int i = 0; i < input_files_count; i++) {
        input_progs[i] = cc_parse(&vm, input_tokens[i]);
        if (!input_progs[i]) {
            // parse() returns NULL when no new globals were created
            // (all declarations mapped to macro_globals). If macro
            // globals exist, use those as the program instead.
            if (vm.compiler.macro_globals) {
                input_progs[i] = vm.compiler.macro_globals;
            } else {
                fprintf(stderr, "error: failed to parse %s\n", input_files[i]);
                goto BAIL;
            }
        }
    }

    // Check for errors after parsing
    if (cc_has_errors(&vm)) {
        cc_print_all_errors(&vm);
        exit_code = 1;
        goto BAIL;
    }

    // For --ffi-decls, emit parsed function/struct/enum declarations. We
    // don't need to link (especially useful for header files without main()).
    if (output_ffi_decls) {
        // Link programs, but don't fail if linking fails (e.g., no main() in
        // header file)
        Obj *merged_prog = cc_link_progs(&vm, input_progs, input_files_count);
        if (!merged_prog && input_files_count == 1) {
            // If linking failed and we have a single file, just use that file's
            // AST
            merged_prog = input_progs[0];
        } else if (!merged_prog) {
            fprintf(stderr, "error: failed to link programs for JSON output\n");
            goto BAIL;
        }

        FILE *f = out_file ? fopen(out_file, "w") : stdout;
        if (!f) {
            fprintf(stderr, "error: failed to open output file %s\n", out_file);
            goto BAIL;
        }
        cc_output_json(f, merged_prog);
        if (f != stdout)
            fclose(f);
        goto BAIL;
    }

    // Link all programs together
    Obj *merged_prog = cc_link_progs(&vm, input_progs, input_files_count);
    if (!merged_prog) {
        fprintf(stderr, "error: failed to link programs\n");
        goto BAIL;
    }

    // Inject inline-macro-generated function definitions into the program.
    // These Objs were created by __builtin_ast_function during pre-parse inline
    // macro execution and stashed in vm.compiler.macro_globals. They must be
    // in the prog list so cc_compile calls gen_function on them and the
    // call-patcher can resolve calls to them by name.
    // If macro_globals IS the main program (parse() returned it because no
    // new globals were created), don't double-prepend it.
    if (vm.compiler.macro_globals && merged_prog != vm.compiler.macro_globals) {
        Obj *tail = vm.compiler.macro_globals;
        while (tail->next)
            tail = tail->next;
        tail->next = merged_prog;
        merged_prog = vm.compiler.macro_globals;
    }

    // Expand macros in the AST
    cc_expand_macros(&vm, merged_prog);

    // Check for errors and warnings after macro expansion (ticket #78)
    if (cc_has_errors(&vm) || vm.warning_count > 0) {
        cc_print_all_errors(&vm);
        if (cc_has_errors(&vm)) {
            exit_code = 1;
            goto BAIL;
        }
    }

    // If -M/--dump-expanded flag is set, output macro-expanded source and exit
    if (dump_expanded_only) {
        FILE *f = out_file ? fopen(out_file, "w") : stdout;
        if (!f) {
            fprintf(stderr, "error: failed to open output file %s\n", out_file);
            goto BAIL;
        }
        cc_serialize_program(f, &vm, merged_prog, emit_generated_only);
        if (f != stdout)
            fclose(f);
        goto BAIL;
    }

    // Merge libraries requested via #pragma cccc link(...) into the
    // -l/--library list (#357), so they get FFI-resolved (and, for -c=native,
    // linked) the same as -l. Copies are made so `libs[]` and
    // vm.compiler.pragma_link_libs each own their own strings (both are freed
    // independently at cleanup).
    for (int i = 0; i < vm.compiler.pragma_link_libs.len; i++) {
        libs = realloc(libs, sizeof(*libs) * (libs_count + 1));
        libs[libs_count++] = strdup(vm.compiler.pragma_link_libs.data[i]);
    }

    if (libs_count > 0)
        register_dynamic_externs(&vm, merged_prog);

    if (print_tokens) {
        for (int i = 0; i < input_files_count; i++) {
            printf("=== Tokens for %s ===\n", input_files[i]);
            cc_print_tokens(input_tokens[i]);
            printf("\n");
        }
        goto BAIL;
    }

    if (dump_ast) {
        FILE *f = out_file ? fopen(out_file, "w") : stdout;
        if (!f) {
            fprintf(stderr, "error: failed to open output file %s\n", out_file);
            exit_code = 1;
            goto BAIL;
        }
        if (output_json)
            cc_dump_ast_json(f, merged_prog, verbose);
        else
            cc_dump_ast(f, merged_prog, verbose);
        if (f != stdout)
            fclose(f);
        goto BAIL;
    }

    // Compile the merged program
    cc_compile(&vm, merged_prog);

    // Check for errors after code generation
    if (cc_has_errors(&vm)) {
        cc_print_all_errors(&vm);
        exit_code = 1;
        goto BAIL;
    }

    // -c/--compile: write bytecode (or hand off to native) and exit. This
    // runs BEFORE the "save bytecode to out_file, then run" branch below so
    // -c=bytecode can write to stdout / arbitrary paths and -c=native can
    // short-circuit out of the VM runtime path. The order is also the fix
    // for ticket #300: previously `compile_only` short-circuited at
    // `goto BAIL;` before the legacy `out_file` save block, silently
    // swallowing `-c -o foo.c4`.
    if (build_mode) {
        // A build script must not define main() in --build mode.
        for (Obj *o = merged_prog; o; o = o->next) {
            if (o->is_function && o->name && strcmp(o->name, "main") == 0) {
                fprintf(stderr,
                        "error: a --build script must not define main()\n");
                exit_code = 1;
                goto BAIL;
            }
        }

        CcNativeCompileArgs build_defaults = {
            .inc_paths      = inc_paths,      .inc_paths_count = inc_paths_count,
            .sys_inc_paths  = sys_inc_paths,  .sys_inc_paths_count = sys_inc_paths_count,
            .lib_paths      = lib_paths,      .lib_paths_count = lib_paths_count,
            .libs           = libs,           .libs_count = libs_count,
            .defines        = defines,        .defines_count = defines_count,
            .undefs         = undefs,         .undefs_count = undefs_count,
            .std_arg        = std_arg,
        };
        CcBuildOptions build_opts = {
            .entry_name       = build_entry,
            .target_name      = build_target,
            .out_dir          = build_out_dir,
            .verbose          = verbose,
            .build_verbose    = build_verbose,
            .quiet            = build_quiet,
            .keep_going       = build_keep_going,
            .dry_run          = build_dry_run,
            .jobs             = build_jobs,
            .defaults         = &build_defaults,
            .tool_allow       = build_tool_allow,
            .tool_allow_count = build_tool_allow_count,
            .list_targets     = build_list_targets,
            .profile          = build_profile,
            .cross_triple     = build_triple,
            .cross_cc         = build_cc,
            .build_cache          = build_cache,
            .cccc_self            = argv[0],
            .build_options        = build_options,
            .build_options_count  = build_options_count,
            .build_install        = build_install,
            .user_args            = (dashdash >= 0 && dashdash + 1 < argc)
                                        ? argv + dashdash + 1 : NULL,
            .user_args_count      = (dashdash >= 0 && dashdash + 1 < argc)
                                        ? argc - dashdash - 1 : 0,
        };

        exit_code = cc_run_build(&vm, merged_prog, &build_opts);

        goto BAIL;   // never fall through to the compile block
    }

    if (testing_mode) {
        CcTestOptions test_opts = {
            .test_glob    = test_glob,
            .suite_filter = suite_filter,
            .list_only    = (bool)list_tests,
            .fail_fast    = (bool)fail_fast,
            .test_timeout = test_timeout,
            .format       = test_format,
        };

        exit_code = cc_run_tests(&vm, merged_prog, &test_opts);

        goto BAIL;   // never fall through to the compile block
    }

    if (compile_format == COMPILE_BYTECODE) {
        // Run the bytecode linker pass: for each --link lib.c4a, append the
        // library into the VM and resolve any pending text relocations (#565).
        if (link_paths_count > 0 && compile_only) {
            fprintf(stderr,
                    "warning: --link has no effect when combined with -c bytecode "
                    "(library output retains its text relocations)\n");
        }
        if (link_paths_count > 0 && !compile_only) {
            for (int i = 0; i < link_paths_count; i++) {
                if (cc_link_bytecode(&vm, link_paths[i]) != 0) {
                    fprintf(stderr, "error: failed to link %s\n", link_paths[i]);
                    exit_code = 1;
                    goto BAIL;
                }
            }
            // Error on any remaining unresolved text relocations.
            for (int i = 0; i < vm.compiler.num_text_relocs; i++) {
                if (!vm.compiler.text_relocs[i].resolved) {
                    fprintf(stderr, "error: unresolved external: %s\n",
                            vm.compiler.text_relocs[i].name
                            ? vm.compiler.text_relocs[i].name : "(unknown)");
                    exit_code = 1;
                }
            }
            // Error on any remaining unresolved address relocations (#566).
            for (int i = 0; i < vm.compiler.num_addr_relocs; i++) {
                if (!vm.compiler.addr_relocs[i].resolved) {
                    fprintf(stderr, "error: unresolved function pointer: %s\n",
                            vm.compiler.addr_relocs[i].name
                            ? vm.compiler.addr_relocs[i].name : "(unknown)");
                    exit_code = 1;
                }
            }
            if (exit_code != 0) goto BAIL;
        }
        if (out_file) {
            if (cc_save_bytecode(&vm, out_file) != 0) {
                fprintf(stderr, "error: failed to save bytecode to %s\n",
                        out_file);
                exit_code = 1;
                goto BAIL;
            }
            fprintf(stderr, "Bytecode saved to %s\n", out_file);
        } else {
            if (CCCC_ISATTY(CCCC_FILENO(stdout))) {
                fprintf(stderr,
                        "error: refusing to write bytecode to a terminal; "
                        "use -o <file> or redirect stdout\n");
                exit_code = 1;
                goto BAIL;
            }
            if (cc_write_bytecode(&vm, stdout) != 0) {
                fprintf(stderr, "error: failed to write bytecode to stdout\n");
                exit_code = 1;
                goto BAIL;
            }
            fflush(stdout);
        }
        goto BAIL;
    }

    if (compile_format == COMPILE_NATIVE) {
        // -c=native is rejected above if out_file is NULL, so exe_path is
        // always a user-supplied path here.
        exit_code = run_native_backend(
            &vm, merged_prog, out_file, inc_paths, inc_paths_count,
            sys_inc_paths, sys_inc_paths_count, lib_paths, lib_paths_count,
            libs, libs_count, defines, defines_count, undefs, undefs_count,
            std_arg);
        goto BAIL;
    }

    if (load_requested_libraries(&vm, libs, libs_count, lib_paths,
                                 lib_paths_count) != 0) {
        exit_code = 1;
        goto BAIL;
    }
    if (verify_dynamic_externs(&vm) != 0) {
        exit_code = 1;
        goto BAIL;
    }

    if (disassemble) {
        cc_disassemble(&vm);
        goto BAIL;
    }

    // Static bytecode analysis on the just-compiled text segment.
    if (run_ngrams || run_fusion) {
        if (run_ngrams) {
            CcAnalyzeNgramOptions opts = {
                .n = run_ngrams,
                .top_n = ngrams_top,
                .per_file = ngrams_per_file,
            };
            ngram_state = cc_analyze_ngram_begin(&opts);
            const char *label = input_files_count == 1
                                     ? input_files[0]
                                     : "<merged source>";
            cc_analyze_ngram_feed(ngram_state, vm.text_seg,
                                  (long long)vm.text_ptr + 1, label, stdout);
            cc_analyze_ngram_finish(ngram_state, stdout);
            ngram_state = NULL;
        } else {
            CcAnalyzeFusionOptions opts = {
                .top_n = run_fusion,
                .json = output_json,
            };
            fusion_state = cc_analyze_fusion_begin(&opts);
            const char *label = input_files_count == 1
                                     ? input_files[0]
                                     : "<merged source>";
            cc_analyze_fusion_feed(fusion_state, vm.text_seg,
                                   (long long)vm.text_ptr + 1, label,
                                   stdout);
            cc_analyze_fusion_finish(fusion_state, stdout);
            fusion_state = NULL;
        }
        goto BAIL;
    }

    if (out_file) {
        // Run the bytecode linker pass for --link libs (#565).
        if (link_paths_count > 0) {
            for (int i = 0; i < link_paths_count; i++) {
                if (cc_link_bytecode(&vm, link_paths[i]) != 0) {
                    fprintf(stderr, "error: failed to link %s\n", link_paths[i]);
                    exit_code = 1;
                    goto BAIL;
                }
            }
            for (int i = 0; i < vm.compiler.num_text_relocs; i++) {
                if (!vm.compiler.text_relocs[i].resolved) {
                    fprintf(stderr, "error: unresolved external: %s\n",
                            vm.compiler.text_relocs[i].name
                            ? vm.compiler.text_relocs[i].name : "(unknown)");
                    exit_code = 1;
                }
            }
            for (int i = 0; i < vm.compiler.num_addr_relocs; i++) {
                if (!vm.compiler.addr_relocs[i].resolved) {
                    fprintf(stderr, "error: unresolved function pointer: %s\n",
                            vm.compiler.addr_relocs[i].name
                            ? vm.compiler.addr_relocs[i].name : "(unknown)");
                    exit_code = 1;
                }
            }
            if (exit_code != 0) goto BAIL;
        }
        // Save bytecode to file and exit (legacy path: no -c, just -o).
        if (cc_save_bytecode(&vm, out_file) != 0) {
            fprintf(stderr, "error: failed to save bytecode to %s\n", out_file);
            exit_code = 1;
            goto BAIL;
        }
        fprintf(stderr, "Bytecode saved to %s\n", out_file);
        goto BAIL;
    }

    // Run the program. If "--" was given, forward only args after it; otherwise
    // fall back to the old behaviour of passing all positional args.
    int prog_argc = 0;
    char **prog_argv =
        build_source_argv(&prog_argc, argc, argv, optind, dashdash);
    exit_code = cc_run(&vm, prog_argc, prog_argv);
    vm_profile_mode = "source";
    vm_profile_input = input_files_count == 1 ? input_files[0] : "multiple";
    vm_profile_ran = 1;
    free(prog_argv);

BAIL:
    if (vm_profile && vm_profile_ran) {
        if (vm_profile_text)
            cc_vm_profile_print(&vm, stderr);
        if (output_json &&
            cc_vm_profile_write_json(&vm, stdout, vm_profile_mode,
                                     vm_profile_input) != 0) {
            fprintf(stderr,
                    "error: failed to write VM profile JSON to stdout\n");
            if (exit_code == 0 || exit_code == 42)
                exit_code = 1;
        }
    }
    cc_destroy(&vm);
    // Defensive: free analysis state if it was allocated but not finalized
    // (shouldn't happen given the dispatch flow, but keeps leak-checkers happy).
    if (ngram_state) {
        cc_analyze_ngram_finish(ngram_state, stdout);
    }
    if (fusion_state) {
        cc_analyze_fusion_finish(fusion_state, stdout);
    }
    if (input_tokens)
        free(input_tokens);
    if (input_progs) {
        // Don't free individual Obj* - they're arena-allocated and freed by
        // cc_destroy()
        free(input_progs);
    }
    if (out_file)
        free(out_file);
    if (inc_paths) {
        for (int i = 0; i < inc_paths_count; i++)
            free((void *)inc_paths[i]);
        free(inc_paths);
    }
    if (lib_paths) {
        for (int i = 0; i < lib_paths_count; i++)
            free((void *)lib_paths[i]);
        free(lib_paths);
    }
    if (libs) {
        for (int i = 0; i < libs_count; i++)
            free((void *)libs[i]);
        free(libs);
    }
    if (ffi_allow_args) {
        for (int i = 0; i < ffi_allow_args_count; i++)
            free((void *)ffi_allow_args[i]);
        free(ffi_allow_args);
    }
    if (ffi_deny_args) {
        for (int i = 0; i < ffi_deny_args_count; i++)
            free((void *)ffi_deny_args[i]);
        free(ffi_deny_args);
    }
    if (sys_inc_paths) {
        for (int i = 0; i < sys_inc_paths_count; i++)
            free((void *)sys_inc_paths[i]);
        free(sys_inc_paths);
    }
    if (defines) {
        for (int i = 0; i < defines_count; i++)
            free((void *)defines[i]);
        free(defines);
    }
    if (undefs) {
        for (int i = 0; i < undefs_count; i++)
            free((void *)undefs[i]);
        free(undefs);
    }
    if (input_files) {
        for (int i = 0; i < input_files_count; i++)
            free((void *)input_files[i]);
        free(input_files);
    }
    free(build_options);
    free(link_paths);
    return exit_code;
}
