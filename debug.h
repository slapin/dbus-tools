#ifndef DEBUG_H
#define DEBUG_H
#include <syslog.h>
void dprint(int prio, const char *fmt, ...);
void debug_init(void);
void d_info(const char *fmt, ...);
void d_debug(const char *fmt, ...);
void d_error(const char *fmt, ...);
void d_notice(const char *fmt, ...);
#endif

