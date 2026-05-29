/*
 JCC: JIT C Compiler

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
#include "jcc.h"
#include <getopt.h>

static void usage(const char *argv0, int exit_code) {
    printf("JCC: JIT C Compiler\n");
    printf("https://git.sr.ht/~takeiteasy/jcc\n\n");
    printf("Usage: %s [options] file...\n\n", argv0);
    printf("Options:\n");
    printf("\t-h/--help           Show this message\n");
    printf("\t-I <path>           Add <path> to include search paths\n");
    printf("\t   --isystem <path> Add <path> to system include paths (for "
           "non-standard headers)\n");
    printf("\t-D <macro>[=def]    Define a macro\n");
    printf("\t-U <macro>          Undefine a macro\n");
    printf("\t-a/--ast            Dump AST\n");
    printf("\t-P/--print-tokens   Print preprocessed tokens to stdout\n");
    printf("\t-E/--preprocess     Output preprocessed source code (traditional "
           "C -E)\n");
    printf("\t-M/--macro-expand   Output macro-expanded source code (for gcc "
           "compatibility)\n");
    printf("\t-j/--json           Output header declarations as JSON\n");
    printf("\t-X/--no-preprocess  Disable preprocessing step\n");
    printf("\t-S/--no-stdlib      Do not link standard library\n");
    printf("\t-o/--out <file>     Dump bytecode to <file> (no execution)\n");
    printf("\t-d/--disassemble    Disassemble bytecode to stdout\n");
    printf("\t-v/--verbose        Enable debug logging\n");
    printf("\t-g/--debug          Enable interactive debugger\n");
    printf("\nSafety Levels (preset flag combinations):\n");
    printf("\t-0/--safety=none     No safety checks (maximum performance)\n");
    printf("\t-1/--safety=basic    Essential low-overhead checks (~5-10%% "
           "overhead)\n");
    printf("\t-2/--safety=standard Comprehensive development safety (~20-40%% "
           "overhead)\n");
    printf("\t-3/--safety=max      All safety features for deep debugging "
           "(~60-100%%+ overhead)\n");
    printf("\nMemory Safety Options (can be combined with safety levels):\n");
    printf("\t-b/--bounds-checks           Runtime array bounds checking\n");
    printf("\t-f/--uaf-detection           Use-after-free detection\n");
    printf("\t-t/--type-checks             Runtime type checking on pointer "
           "dereferences\n");
    printf("\t-z/--uninitialized-detection Uninitialized variable detection\n");
    printf("\t-O/--overflow-checks         Detect signed integer overflow\n");
    printf("\t-s/--stack-canaries          Stack overflow protection\n");
    printf("\t-k/--heap-canaries           Heap overflow protection\n");
    printf("\t-l/--memory-leak-detection   Track allocations and report leaks "
           "at exit\n");
    printf("\t-i/--stack-instrumentation   Track stack variable lifetimes and "
           "accesses\n");
    printf("\t   --stack-errors            Enable runtime errors for stack "
           "instrumentation\n");
    printf("\t-p/--pointer-sanitizer       Enable all pointer checks (bounds, "
           "UAF, type)\n");
    printf("\t   --dangling-pointers       Detect use of stack pointers after "
           "function return\n");
    printf(
        "\t   --alignment-checks        Validate pointer alignment for type\n");
    printf("\t   --provenance-tracking     Track pointer origin and validate "
           "operations\n");
    printf("\t   --invalid-arithmetic      Detect pointer arithmetic outside "
           "object bounds\n");
    printf("\t-F/--format-string-checks    Validate format strings in "
           "printf-family functions\n");
    printf("\t   --random-canaries         Use random stack canaries (prevents "
           "predictable bypass)\n");
    printf("\t   --memory-poisoning        Poison allocated/freed memory "
           "(0xCD/0xDD patterns)\n");
    printf("\t-T/--memory-tagging          Temporal memory tagging (track "
           "pointer generation tags)\n");
    printf("\t-V/--vm-heap                 Route all malloc/free through VM "
           "heap (enables memory safety)\n");
    printf("\nPreprocessor Options:\n");
    printf("\t   --embed-limit=SIZE        Set #embed file size warning limit "
           "(e.g., 50MB, 100mb, default: 10MB)\n");
    printf("\t   --embed-hard-limit        Make #embed limit a hard error "
           "instead of warning\n");
    printf("\nOptimization Levels:\n");
    printf("\t   --optimize[=LEVEL]        Enable bytecode optimization "
           "(default: disabled)\n");
    printf("\t                             LEVEL: 0=none, 1=basic, 2=standard, "
           "3=aggressive\n");
    printf("\t                             -O0: No optimization\n");
    printf("\t                             -O1: Constant folding only\n");
    printf("\t                             -O2: Constant folding + peephole\n");
    printf("\t                             -O3: All optimizations (including "
           "dead code elimination)\n");
    printf("\nExample:\n");
    printf("\t%s -o hello hello.c\n", argv0);
    printf("\t%s -I ./include -D DEBUG -o prog prog.c\n", argv0);
    printf("\techo 'int main() { return 42; }' | %s -\n", argv0);
    printf("\n");
    exit(exit_code);
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
    char template[] = "/tmp/jcc-stdin-XXXXXX";
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

static void parse_define(JCC *vm, char *arg) {
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

int main(int argc, const char *argv[]) {
    int exit_code = 0;
    const char **input_files = NULL;
    int input_files_count = 0;
    Obj **input_progs = NULL;
    Token **input_tokens = NULL;
    const char **inc_paths = NULL; // -I
    int inc_paths_count = 0;
    const char **sys_inc_paths = NULL; // -isystem
    int sys_inc_paths_count = 0;
    const char **defines = NULL; // -D
    int defines_count = 0;
    const char **undefs = NULL; // -U
    int undefs_count = 0;
    char *out_file = NULL;     // -o (single)
    int dump_ast = 0;          // -a
    int disassemble = 0;       // -d
    int verbose = 0;           // -v
    uint32_t flags = 0;        // JCCFlags bitfield for runtime features
    int print_tokens = 0;      // -P
    int preprocess_only = 0;   // -E
    int macro_expand_only = 0; // -M
    int skip_preprocess = 0;   // -X
    int skip_stdlib = 0;       // -S
    int output_json = 0;       // -j
#ifdef JCC_HAS_CURL
    char *url_cache_dir = NULL; // --url-cache-dir
    int url_cache_clear = 0;    // --url-cache-clear
#endif
    int max_errors = 20;        // --max-errors (default: 20)
    int warnings_as_errors = 0; // --Werror
    size_t embed_limit = 0;     // --embed-limit (0 = use default)
    int embed_hard_error = 0;   // --embed-hard-limit
    int opt_level = 0; // -O0/-O1/-O2/-O3 (default: 0 = no optimization)

    if (argc <= 1)
        usage(argv[0], 1);

    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"out", required_argument, 0, 'o'},
        {"disassemble", no_argument, 0, 'd'},
        {"verbose", no_argument, 0, 'v'},
        {"ast", no_argument, 0, 'a'},
        {"print-tokens", no_argument, 0, 'P'},
        {"preprocess", no_argument, 0, 'E'},
        {"macro-expand", no_argument, 0, 'M'},
        {"no-preprocess", no_argument, 0, 'X'},
        {"no-stdlib", no_argument, 0, 'S'},
        {"json", no_argument, 0, 'j'},
        {"debug", no_argument, 0, 'g'},
        {"safety", required_argument, 0, 1012},
        {"bounds-checks", no_argument, 0, 'b'},
        {"uaf-detection", no_argument, 0, 'f'},
        {"type-checks", no_argument, 0, 't'},
        {"uninitialized-detection", no_argument, 0, 'z'},
        {"overflow-checks", no_argument, 0, 'O'},
        {"stack-canaries", no_argument, 0, 's'},
        {"heap-canaries", no_argument, 0, 'k'},
        {"pointer-sanitizer", no_argument, 0, 'p'},
        {"memory-leak-detection", no_argument, 0, 'l'},
        {"stack-instrumentation", no_argument, 0, 'i'},
        {"stack-errors", no_argument, 0, 1005},
        {"dangling-pointers", no_argument, 0, 1001},
        {"alignment-checks", no_argument, 0, 1002},
        {"provenance-tracking", no_argument, 0, 1003},
        {"invalid-arithmetic", no_argument, 0, 1004},
        {"format-string-checks", no_argument, 0, 'F'},
        {"random-canaries", no_argument, 0, 1006},
        {"memory-poisoning", no_argument, 0, 1007},
        {"memory-tagging", no_argument, 0, 'T'},
        {"vm-heap", no_argument, 0, 'V'},
        {"control-flow-integrity", no_argument, 0, 'C'},
        {"include", required_argument, 0, 'I'},
        {"isystem", required_argument, 0, 1013},
        {"define", required_argument, 0, 'D'},
        {"undef", required_argument, 0, 'U'},
        {"url-cache-dir", required_argument, 0, 1008},
        {"url-cache-clear", no_argument, 0, 1009},
        {"max-errors", required_argument, 0, 1010},
        {"Werror", no_argument, 0, 1011},
        {"embed-limit", required_argument, 0, 1014},
        {"embed-hard-limit", no_argument, 0, 1015},
        {"optimize", optional_argument, 0, 1016},
        {0, 0, 0, 0}};

    const char *optstring = "0123haI:D:U:o:dvgbftzOskpliPEMXSjFTVC";
    int opt;
    opterr = 0; // we'll handle errors explicitly
    while ((opt = getopt_long(argc, (char *const *)argv, optstring,
                              long_options, NULL)) != -1) {
        switch (opt) {
        case 'h':
            usage(argv[0], 0);
            break;
        case '0':
            // Safety level 0: None - explicitly clear all safety flags
            flags = 0;
            break;
        case '1':
            // Safety level 1: Basic - essential low-overhead checks
            flags |= JCC_SAFETY_BASIC;
            break;
        case '2':
            // Safety level 2: Standard - comprehensive development safety
            flags |= JCC_SAFETY_STANDARD;
            break;
        case '3':
            // Safety level 3: Maximum - all safety features
            flags |= JCC_SAFETY_MAX;
            break;
        case 1012:
            // --safety=<level> flag
            if (strncmp(optarg, "none", sizeof("none")) == 0 ||
                strncmp(optarg, "0", sizeof("0")) == 0) {
                flags = 0;
            } else if (strncmp(optarg, "basic", sizeof("basic")) == 0 ||
                       strncmp(optarg, "1", sizeof("1")) == 0) {
                flags |= JCC_SAFETY_BASIC;
            } else if (strncmp(optarg, "standard", sizeof("standard")) == 0 ||
                       strncmp(optarg, "2", sizeof("2")) == 0) {
                flags |= JCC_SAFETY_STANDARD;
            } else if (strncmp(optarg, "max", sizeof("max")) == 0 ||
                       strncmp(optarg, "3", sizeof("3")) == 0) {
                flags |= JCC_SAFETY_MAX;
            } else {
                fprintf(stderr,
                        "error: invalid safety level '%s' (use "
                        "none/basic/standard/max or 0/1/2/3)\n",
                        optarg);
                usage(argv[0], 1);
            }
            break;
        case 'o':
            if (out_file) {
                fprintf(stderr, "error: only one -o/--out allowed\n");
                usage(argv[0], 1);
            }
            out_file = strdup(optarg);
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
            flags |= JCC_ENABLE_DEBUGGER;
            break;
        case 'I':
            inc_paths =
                realloc(inc_paths, sizeof(*inc_paths) * (inc_paths_count + 1));
            inc_paths[inc_paths_count++] = strdup(optarg);
            break;
        case 1013: // --isystem
            sys_inc_paths =
                realloc(sys_inc_paths,
                        sizeof(*sys_inc_paths) * (sys_inc_paths_count + 1));
            sys_inc_paths[sys_inc_paths_count++] = strdup(optarg);
            break;
        case 'D':
            defines = realloc(defines, sizeof(*defines) * (defines_count + 1));
            defines[defines_count++] = strdup(optarg);
            break;
        case 'U':
            undefs = realloc(undefs, sizeof(*undefs) * (undefs_count + 1));
            undefs[undefs_count++] = strdup(optarg);
            break;
        case 'b':
            flags |= JCC_BOUNDS_CHECKS;
            break;
        case 'f':
            flags |= JCC_UAF_DETECTION;
            break;
        case 't':
            flags |= JCC_TYPE_CHECKS;
            break;
        case 'z':
            flags |= JCC_UNINIT_DETECTION;
            break;
        case 'O':
            flags |= JCC_OVERFLOW_CHECKS;
            break;
        case 's':
            flags |= JCC_STACK_CANARIES;
            break;
        case 'k':
            flags |= JCC_HEAP_CANARIES;
            break;
        case 'p':
            flags |= JCC_POINTER_SANITIZER;
            break;
        case 'l':
            flags |= JCC_MEMORY_LEAK_DETECT;
            break;
        case 'i':
            flags |= JCC_STACK_INSTR;
            break;
        case 1001:
            flags |= JCC_DANGLING_DETECT;
            break;
        case 1002:
            flags |= JCC_ALIGNMENT_CHECKS;
            break;
        case 1003:
            flags |= JCC_PROVENANCE_TRACK;
            break;
        case 1004:
            flags |= JCC_INVALID_ARITH;
            break;
        case 1005:
            flags |= JCC_STACK_INSTR_ERRORS;
            break;
        case 'F':
            flags |= JCC_FORMAT_STR_CHECKS;
            break;
        case 1006:
            flags |= JCC_RANDOM_CANARIES;
            break;
        case 1007:
            flags |= JCC_MEMORY_POISONING;
            break;
        case 'T':
            flags |= JCC_MEMORY_TAGGING;
            break;
        case 'V':
            flags |= JCC_VM_HEAP;
            break;
        case 'C':
            flags |= JCC_CFI;
            break;
        case 'P':
            print_tokens = 1;
            break;
        case 'E':
            preprocess_only = 1;
            break;
        case 'M':
            macro_expand_only = 1;
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
#ifdef JCC_HAS_CURL
        case 1008:
            url_cache_dir = strdup(optarg);
            break;
        case 1009:
            url_cache_clear = 1;
            break;
#endif
        case 1010:
            max_errors = atoi(optarg);
            if (max_errors <= 0) {
                fprintf(stderr,
                        "error: --max-errors must be a positive integer\n");
                usage(argv[0], 1);
            }
            break;
        case 1011:
            warnings_as_errors = 1;
            break;
        case 1014: // --embed-limit
            embed_limit = parse_size(optarg, "--embed-limit");
            break;
        case 1015: // --embed-hard-limit
            embed_hard_error = 1;
            break;
        case 1016: // --optimize (or -O)
            if (optarg == NULL) {
                // Just -O or --optimize without argument means -O1
                opt_level = 1;
            } else if (optarg[0] >= '0' && optarg[0] <= '3' &&
                       optarg[1] == '\0') {
                opt_level = optarg[0] - '0';
            } else {
                fprintf(stderr,
                        "error: invalid optimization level '%s' (use 0, 1, 2, "
                        "or 3)\n",
                        optarg);
                usage(argv[0], 1);
            }
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

    /* Remaining arguments are input files (positional) */
    for (int i = optind; i < argc; i++) {
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

    // If the only input file is "-", read stdin into a temporary file and
    // replace it
    if (input_files_count == 1 &&
        strncmp(input_files[0], "-", sizeof("-")) == 0) {
        char *tmp = read_stdin_to_tmp();
        if (!tmp) {
            fprintf(stderr,
                    "error: failed to read stdin into temporary file\n");
            // cleanup before exit
            for (int i = 0; i < inc_paths_count; i++)
                free((void *)inc_paths[i]);
            free(inc_paths);
            for (int i = 0; i < sys_inc_paths_count; i++)
                free((void *)sys_inc_paths[i]);
            free(sys_inc_paths);
            for (int i = 0; i < defines_count; i++)
                free((void *)defines[i]);
            free(defines);
            for (int i = 0; i < undefs_count; i++)
                free((void *)undefs[i]);
            free(undefs);
            for (int i = 0; i < input_files_count; i++)
                free((void *)input_files[i]);
            free(input_files);
            free(out_file);
            usage((char *)argv[0], 1);
        }
        free((void *)input_files[0]);
        input_files[0] = tmp;
    }

    JCC vm;
    cc_init(&vm, flags);

    if (verbose)
        vm.debug_vm = 1;

    // Check if input is a bytecode file (.jbc extension)
    // If so, load and run it directly without compilation
    if (input_files_count == 1) {
        const char *input_file = input_files[0];
        size_t len = strlen(input_file);
        if (len > 4 &&
            strncmp(input_file + len - 4, ".jbc", sizeof(".jbc")) == 0) {
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

            // Run the loaded bytecode
            exit_code = cc_run(&vm, argc, (char **)argv);
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

    // If random canaries are enabled, regenerate the stack canary
    if (vm.flags & JCC_RANDOM_CANARIES) {
        vm.stack_canary = generate_random_canary();
    }

#ifdef JCC_HAS_CURL
    // Configure URL cache if needed
    if (url_cache_dir) {
        vm.compiler.url_cache_dir = url_cache_dir;
    }
    if (url_cache_clear) {
        clear_url_cache(&vm);
    }
#endif

    // Enable error collection for better error reporting
    vm.collect_errors = true;
    vm.max_errors = max_errors;
    vm.warnings_as_errors = warnings_as_errors;
    jmp_buf err_buf;
    vm.error_jmp_buf = &err_buf;

    // Set up error handling with setjmp/longjmp
    if (setjmp(err_buf) != 0) {
        // Error occurred during compilation
        cc_print_all_errors(&vm);
        exit_code = 1;
        goto BAIL;
    }

    if (!skip_stdlib)
        cc_load_stdlib(&vm);

    // Add JCC's standard library header directory
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

    // If -E flag is set, output preprocessed source and exit
    if (preprocess_only) {
        for (int i = 0; i < input_files_count; i++) {
            FILE *f = out_file ? fopen(out_file, "w") : stdout;
            if (!f) {
                fprintf(stderr, "error: failed to open output file %s\n",
                        out_file);
                goto BAIL;
            }

            cc_output_preprocessed(f, input_tokens[i]);
            if (f != stdout)
                fclose(f);
        }
        goto BAIL;
    }

    input_progs = calloc(input_files_count, sizeof(Obj *));
    for (int i = 0; i < input_files_count; i++) {
        input_progs[i] = cc_parse(&vm, input_tokens[i]);
        if (!input_progs[i]) {
            fprintf(stderr, "error: failed to parse %s\n", input_files[i]);
            goto BAIL;
        }
    }

    // Check for errors after parsing
    if (cc_has_errors(&vm)) {
        cc_print_all_errors(&vm);
        exit_code = 1;
        goto BAIL;
    }

    // For JSON output, we don't need to link (especially useful for header
    // files without main())
    if (output_json && !dump_ast) {
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

    // Expand pragma macros in the AST
    cc_expand_pragma_macros(&vm, merged_prog);

    // Check for errors after macro expansion
    if (cc_has_errors(&vm)) {
        cc_print_all_errors(&vm);
        exit_code = 1;
        goto BAIL;
    }

    // If -M flag is set, output macro-expanded source and exit
    if (macro_expand_only) {
        FILE *f = out_file ? fopen(out_file, "w") : stdout;
        if (!f) {
            fprintf(stderr, "error: failed to open output file %s\n", out_file);
            goto BAIL;
        }
        cc_serialize_program(f, &vm, merged_prog);
        if (f != stdout)
            fclose(f);
        goto BAIL;
    }

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

    if (disassemble) {
        cc_disassemble(&vm);
        goto BAIL;
    }

    if (out_file) {
        // Save bytecode to file and exit
        if (cc_save_bytecode(&vm, out_file) != 0) {
            fprintf(stderr, "error: failed to save bytecode to %s\n", out_file);
            exit_code = 1;
            goto BAIL;
        }
        printf("Bytecode saved to %s\n", out_file);
        goto BAIL;
    }

    // Run the program
    exit_code = cc_run(&vm, argc, (char **)argv);

BAIL:
    cc_destroy(&vm);
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
    return exit_code;
}
