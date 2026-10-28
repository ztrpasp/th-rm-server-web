#ifndef RM_WEB_H
#define RM_WEB_H

#include <stdint.h>

int rm_web_start(void);
void rm_web_stop(void);
int rm_web_is_running(void);
uint16_t rm_web_bound_port(void);

#endif
