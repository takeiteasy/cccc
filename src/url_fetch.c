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

bool is_url(const char *filename) {
    return (strncmp(filename, "http://", 7) == 0) ||
           (strncmp(filename, "https://", 8) == 0);
}

#ifdef CCCC_HAS_CURL
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <curl/curl.h>

// Fetch knobs live in vm->compiler (url_timeout / url_max_size), defaulted
// in vm.c's init and overridable via --url-timeout/--url-max-size. One cap
// governs every fetched payload (URL #include headers and URL #embed data
// alike); it is independent of the #embed limits, which still apply to the
// embed itself afterwards.

void init_url_cache(VirtualMachine *vm) {
    if (!vm->compiler.url_cache_dir) {
        // Set default cache directory to platform-specific temp path
        char *temp_dir = NULL;

#ifdef _WIN32
        // Windows: use TEMP or TMP environment variable
        temp_dir = getenv("TEMP");
        if (!temp_dir)
            temp_dir = getenv("TMP");
        if (!temp_dir)
            temp_dir = "C:\\Temp";
#else
        // Unix-like (Linux/macOS): use TMPDIR environment variable or /tmp
        temp_dir = getenv("TMPDIR");
        if (!temp_dir)
            temp_dir = "/tmp";
#endif

        vm->compiler.url_cache_dir = format("%s/.cccc", temp_dir);
    }

    // Create cache directory if it doesn't exist
    struct stat st;
    if (stat(vm->compiler.url_cache_dir, &st) != 0) {
// Directory doesn't exist, create it
#ifdef _WIN32
        mkdir(vm->compiler.url_cache_dir);
#else
        mkdir(vm->compiler.url_cache_dir, 0755);
#endif
    }
}

// #1324: recursively remove `path` (file or directory tree) -- clear_url_
// cache() below now has to remove the URL-shaped mirror tree
// (mirror_cache_entry()) alongside the flat cache entries, and unlink() on a
// non-empty directory just fails silently, leaving the mirror to survive
// --url-cache-clear forever.
static void remove_path_recursive(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0)
        return;
    if (!S_ISDIR(st.st_mode)) {
        unlink(path);
        return;
    }
    DIR *dir = opendir(path);
    if (!dir)
        return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        char *child = format("%s/%s", path, entry->d_name);
        remove_path_recursive(child);
        free(child);
    }
    closedir(dir);
    rmdir(path);
}

void clear_url_cache(VirtualMachine *vm) {
    if (!vm->compiler.url_cache_dir)
        return;

    DIR *dir = opendir(vm->compiler.url_cache_dir);
    if (!dir)
        return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;                // Skip . and ..

        char *path = format("%s/%s", vm->compiler.url_cache_dir, entry->d_name);
        remove_path_recursive(path); // #1324: was a plain unlink()
        free(path);
    }
    closedir(dir);
}

static unsigned long hash_url(const char *url) {
    unsigned long hash = 5381;
    int           c;
    while ((c = *url++))
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    return hash;
}

// #1324: build the on-disk mirror path for `url` under
// vm->compiler.url_cache_dir -- exactly the path a host preprocessor forms
// when it path-joins a search directory against a directive operand spelled
// `url` verbatim (a run of consecutive '/' collapses to one on every POSIX
// filesystem, so "https://host/x" joined against a search directory resolves
// identically to "https:/host/x" under that directory). Mirroring the fetch
// there lets a URL #include reached only through an ordinary project header
// (never auto-captured -- see the auto-capture gate in preprocess.c, whose
// only inputs are command-line files and CCCC's own bundled/cccc-only
// headers) still resolve as written, once that directory is forwarded via
// `-idirafter` (src/main.c) or named in a banner (-m/-c=generated,
// src/serialize_program.c) -- with no rewriting anywhere in the include
// graph. Returns NULL (skip mirroring; the flat cache entry #1313 already
// relies on is untouched either way) for a shape that can't be safely
// materialized: a `.`/`..` path component, an empty final component (a URL
// ending in '/'), or a result that would exceed PATH_MAX.
static char *url_mirror_path(VirtualMachine *vm, const char *url) {
    size_t cache_len = strlen(vm->compiler.url_cache_dir);
    size_t url_len   = strlen(url);
    if (cache_len + 1 + url_len + 1 > PATH_MAX)
        return NULL;
    char *copy = strdup(url);
    if (!copy)
        return NULL;
    bool ok             = true;
    bool saw_final_part = false;
    for (char *save = NULL, *part = strtok_r(copy, "/", &save); part;
         part = strtok_r(NULL, "/", &save)) {
        if (!strcmp(part, ".") || !strcmp(part, "..")) {
            ok = false;
            break;
        }
        saw_final_part = true;
    }
    free(copy);
    if (!ok || !saw_final_part)
        return NULL;
    // Join with single '/' separators (equivalent to the raw "cache_dir/url"
    // concatenation once the OS collapses the doubled slash after the
    // scheme, but this way mkdir_p_parents() never has to walk an empty
    // path component).
    char  *joined = format("%s/%s", vm->compiler.url_cache_dir, url);
    size_t len    = strlen(joined);
    char  *out    = malloc(len + 1);
    if (!out) {
        free(joined);
        return NULL;
    }
    size_t w = 0;
    for (size_t r = 0; r < len; r++) {
        if (joined[r] == '/' && w > 0 && out[w - 1] == '/')
            continue;
        out[w++] = joined[r];
    }
    out[w] = '\0';
    free(joined);
    return out;
}

// #1324: mkdir -p over every parent directory of `path` (0755, matching
// init_url_cache()'s own permissions), skipping the leading empty component
// of an absolute path.
static void mkdir_p_parents(char *path) {
    for (char *p = path + 1; *p; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        mkdir(path, 0755);
        *p = '/';
    }
}

// #1324: hardlink `cache_path` (the flat #1313 cache entry, already
// downloaded/verified) into its URL-shaped mirror location so the raw
// directive resolves as written; falls back to a byte copy when link()
// can't (cross-device, or a filesystem without hardlinks). Sets
// vm->compiler.url_mirror_used on success so main.c/serialize_program.c know
// to forward/announce the cache directory. Best-effort: a mirroring failure
// is silently skipped, not an error -- the flat cache entry and #1313's own
// rewrite already make the top-level case work regardless.
static void mirror_cache_entry(VirtualMachine *vm, const char *cache_path,
                               const char *url) {
    char *mirror_path = url_mirror_path(vm, url);
    if (!mirror_path)
        return;
    mkdir_p_parents(mirror_path);
    unlink(mirror_path); // drop a stale mirror (e.g. cache entry replaced)
    if (link(cache_path, mirror_path) != 0) {
        // Cross-device or no hardlink support: fall back to a byte copy.
        FILE *src = fopen(cache_path, "rb");
        FILE *dst = src ? fopen(mirror_path, "wb") : NULL;
        if (src && dst) {
            char   buf[8192];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
                fwrite(buf, 1, n, dst);
        }
        if (src)
            fclose(src);
        if (dst)
            fclose(dst);
        else {
            free(mirror_path);
            return;
        }
    }
    vm->compiler.url_mirror_used = true;
    free(mirror_path);
}

static char *get_url_cache_path(VirtualMachine *vm, const char *url) {
    // Extract filename from URL if possible
    const char *last_slash = strrchr(url, '/');
    const char *filename   = last_slash ? last_slash + 1 : "downloaded.h";

    // If no extension or too long, use hash
    const char *dot = strrchr(filename, '.');
    if (!dot || strlen(filename) > 64) {
        unsigned long hash = hash_url(url);
        return format("%s/%lu.h", vm->compiler.url_cache_dir, hash);
    }

    // Use filename from URL with hash prefix to avoid collisions
    unsigned long hash = hash_url(url);
    return format("%s/%lu_%s", vm->compiler.url_cache_dir, hash, filename);
}

// Callback for curl to write data to file
static size_t write_callback(void *ptr, size_t size, size_t nmemb,
                             void *stream) {
    return fwrite(ptr, size, nmemb, (FILE *)stream);
}

char *fetch_url_to_cache(VirtualMachine *vm, const char *url) {
    // Ensure cache directory exists
    init_url_cache(vm);

    // Generate cache path
    char *cache_path = get_url_cache_path(vm, url);

    // Check if already cached
    struct stat st;
    if (stat(cache_path, &st) == 0) {
        // File exists in cache; the size cap still applies so
        // --url-max-size behaves the same whether or not the copy is
        // already local
        if ((size_t)st.st_size > vm->compiler.url_max_size)
            return NULL;
        mirror_cache_entry(vm, cache_path, url); // #1324
        return cache_path;
    }

    // Initialize curl
    CURL *curl = curl_easy_init();
    if (!curl) {
        return NULL;
    }

    // Open output file
    FILE *fp = fopen(cache_path, "wb");
    if (!fp) {
        curl_easy_cleanup(curl);
        return NULL;
    }

    // Set curl options
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); // Follow redirects
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)vm->compiler.url_timeout);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER,
                     1L); // Verify SSL certificates
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "cccc-compiler/1.0");

    // Perform the request
    CURLcode res = curl_easy_perform(curl);

    // Get HTTP response code
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    // Cleanup
    fclose(fp);
    curl_easy_cleanup(curl);

    // Check for errors
    if (res != CURLE_OK) {
        unlink(cache_path); // Remove partial/failed download
        return NULL;
    }

    if (response_code < 200 || response_code >= 300) {
        unlink(cache_path); // Remove error response
        return NULL;
    }

    // Reject oversized payloads
    if (stat(cache_path, &st) == 0) {
        if ((size_t)st.st_size > vm->compiler.url_max_size) {
            unlink(cache_path);
            return NULL;
        }
    }

    mirror_cache_entry(vm, cache_path, url); // #1324
    return cache_path;
}
#else
void init_url_cache(VirtualMachine *vm) {
    (void)vm;
}

void clear_url_cache(VirtualMachine *vm) {
    (void)vm;
}

char *fetch_url_to_cache(VirtualMachine *vm, const char *url) {
    (void)vm;
    (void)url;
    return NULL; // URL support not compiled in
}
#endif // CCCC_HAS_CURL
