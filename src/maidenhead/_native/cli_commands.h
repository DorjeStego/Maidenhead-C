#ifndef MAIDENHEAD_NATIVE_CLI_COMMANDS_H
#define MAIDENHEAD_NATIVE_CLI_COMMANDS_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*mh_cli_handler)(int argc, char **argv, FILE *out, FILE *err);

typedef struct {
    const char *name;
    mh_cli_handler handler;
} mh_cli_command;

const mh_cli_command *mh_cli_find_command(const char *name);

#ifdef __cplusplus
}
#endif

#endif
