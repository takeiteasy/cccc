#ifndef CCCC_CONFIG_H
#define CCCC_CONFIG_H

#include <getopt.h>

// The command-line option table, shared by main() and the config loader.
extern const struct option cccc_long_options[];
extern const char *const   cccc_optstring;

// Whether an option may appear in a config file.
typedef enum {
    CFG_FREE,  // applies from any config
    CFG_TRUST, // widens what runs or where cccc writes; local files need trust
    CFG_CLI_ONLY,  // modes and actions; never read from a config
    CFG_USER_ONLY, // the user's own config and the command line, not a project
} CfgClass;

// Class of a config key (a long option name or a short option letter), or -1
// when the key has no class.
int cccc_config_class(const char *key);

// The user config directory (malloc'd), or NULL when there is no home.
char *cccc_config_dir(void);

// Returns a fresh argv holding the global config's options, the trusted
// project config's options, then the command line. Updates *argc. Exits on a
// config error and for --trust, --untrust and --show-config.
const char **cccc_config_expand(int *argc, const char **argv);

#endif
