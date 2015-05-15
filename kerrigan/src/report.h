#ifndef __REPORT_H__
#define __REPORT_H__

#include <stdbool.h>
#include <stdint.h>

#define REPORT() reporter_report(__FUNCTION__)

extern bool reporter_init(const char *server_addr, uint16_t port);
extern void reporter_report(const char *func_name);
extern void reporter_cleanup(void);

#endif
