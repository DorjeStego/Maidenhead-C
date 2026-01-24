#ifndef MAIDENHEAD_NATIVE_CLI_H
#define MAIDENHEAD_NATIVE_CLI_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

int mh_cli_main(int argc, char **argv);
int mh_cli_main_io(int argc, char **argv, FILE *out, FILE *err);

#ifdef __cplusplus
}
#endif

#endif
