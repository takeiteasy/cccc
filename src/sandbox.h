#ifndef CCCC_SANDBOX_H
#define CCCC_SANDBOX_H

#include <stdbool.h>
#include <stddef.h>

// Paths --sandbox leaves writable. NULL members are skipped.
typedef struct {
    const char *out_file;      // -o file; its directory is writable
    bool        default_out;   // an output is written to the working directory
    const char *deps_file;     // --deps-file
    const char *url_cache_dir; // --url-cache-dir
    const char *build_out_dir; // --build-out-dir
    const char *build_cache;   // --build-cache=PATH
    const char *self;          // the running cccc, which --build runs again
} CcccSandboxPaths;

// Confines this process and its children: no network, writes only under the
// given paths and the temp directories, and exec only of cccc and the host C
// toolchain. Irreversible. Returns false and fills `err` when the platform
// cannot enforce it.
bool cccc_sandbox_enter(const CcccSandboxPaths *paths, char *err,
                        size_t err_size);

#endif
