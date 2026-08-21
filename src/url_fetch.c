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

void clear_url_cache(VirtualMachine *vm) {
    if (!vm->compiler.url_cache_dir)
        return;

    DIR *dir = opendir(vm->compiler.url_cache_dir);
    if (!dir)
        return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.')
            continue; // Skip . and ..

        char *path = format("%s/%s", vm->compiler.url_cache_dir, entry->d_name);
        unlink(path);
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
