/* This is the header for the fault injection config parser
   (c) 2019 by Peer Adelt / Paderborn University */

#ifndef FI_PARSER_H_
#define FI_PARSER_H_

int testsetup_load(const char *filename);
int mutantlist_load(const char *filename);
int fear5_gotonext_mutant(void);
void mutantlist_close(void);

#endif