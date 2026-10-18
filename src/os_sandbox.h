#ifndef CCCC_OS_SANDBOX_H
#define CCCC_OS_SANDBOX_H

#include <stdbool.h>
#include <stddef.h>

// Paths --sandbox leaves writable or readable. NULL members are skipped.
typedef struct {
    const char *out_file;      // -o file; its directory is writable
    bool        default_out;   // an output is written to the working directory
    const char *deps_file;     // --deps-file
    const char *url_cache_dir; // --url-cache-dir
    const char *build_out_dir; // --build-out-dir
    const char *build_cache;   // --build-cache=PATH
    const char *self;          // the running cccc, which --build runs again

    // Readable besides the toolchain and system directories: directories as
    // they are, files by their directory.
    const char *const *read_paths;
    int                read_count;
    const char *const *inputs; // input files; their directories are readable
    int                inputs_count;
    // Flags the toolchain probe passes to the host cc (--link-arg), so the
    // programs it names are the ones the link will run.
    const char *const *link_args;
    int                link_args_count;
    // Programs the run may exec in addition to the toolchain (--sandbox-exec).
    const char *const *exec_paths;
    int                exec_count;
    // Whether the run can reach the host C compiler. Only then is it probed
    // and granted; `cc` is the compiler it would use.
    bool        toolchain;
    const char *cc;
    // The run executes the program it builds (--testing=native), so the
    // output directory is executable too.
    bool exec_output;
} CcccSandboxPaths;

// Confines this process and its children: no network, writes only under the
// given paths and the temp directories, reads only under the given paths, the
// working directory, the config directory and the system directories, and exec
// only of cccc, the `exec_paths` and, when `toolchain` is set, the exact
// programs the host C toolchain runs. Irreversible. Returns false and fills
// `err` when the platform cannot enforce it.
bool cccc_sandbox_enter(const CcccSandboxPaths *paths, char *err,
                        size_t err_size);

#endif
