/* This is the header for the fault injection config parser
   (c) 2019 by Peer Adelt / Paderborn University */

#ifndef FI_LOGGER_H_
#define FI_LOGGER_H_

#include <inttypes.h>

void fi_set_logfile(const char *path);
void fi_log_header(void);
void fi_log_footer(void);
void fi_log_goldenrun(uint64_t time, uint64_t time_max);
void fi_log_mutant(uint64_t time, uint64_t time_max, uint32_t code);

#endif