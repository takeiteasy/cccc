#ifndef CCCC_TOML_H
#define CCCC_TOML_H

#include "tomlc17.h"

// TOML loading shared by the config files and the build manifest. Errors print
// `error: <file>:<line>: <message>` and exit 1.

// Parses `path`. Every datum carries the file name and line. Free with
// toml_free().
toml_result_t cccc_toml_load(const char *path);

// Exits with an error located at `at`.
void cccc_toml_fail(toml_datum_t at, const char *fmt, ...);

// Exits if any key or string under `d` holds a control character, since it
// could rewrite terminal text where the value is echoed.
void cccc_toml_check_text(toml_datum_t d);

// "string", "integer", "table", ...
const char *cccc_toml_type_name(toml_type_t type);

#endif
