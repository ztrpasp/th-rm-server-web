#ifndef __RM_CLI_H__
#define __RM_CLI_H__

#include <stdarg.h>
#include <stdbool.h>
#include <stdatomic.h>

#define RM_CLI_RX_BUFFER_SIZE 512

void print_to_cli(const char *msg);
void rm_cli_notify(const char *msg);
void cli_print(const char *fmt, ...);
int cli_main_loop(__attribute__((unused)) void *dummy);

extern _Atomic bool force_quit;

#endif
