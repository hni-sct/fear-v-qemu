#include <stdio.h>
#include <math.h>
#include <inttypes.h>
#include "fear5/faultinjection.h"
#include "fear5/logger.h"

#define FLUSH_DIV 100
static FILE *logfile = NULL;
static int flush_div = 0;

static const char *mutant_result_text[] = {
    "not killed",
    "output deviation",
    "timeout",
    "exception",
    "unknown",
    "interrupt",
    "missing isa extension",
    "non-zero exitcode",
};

static const char *base_exceptions_text[] = {
    "ex::INTERRUPT",
    "ex::HLT",
    "ex::DEBUG",
    "ex::HALTED",
    "ex::YIELD",
    "ex::ATOMIC",
};

static const char *riscv_exceptions_text[] = {
    "ex::INSN_MISA",
    "ex::INSN_AC_FLT",
    "ex::ILLEGAL",
    "ex::BRK_POINT",
    "ex::LD_MISA",
    "ex::LD_AC_FLT",
    "ex::ST_MISA",
    "ex::ST_AC_FLT",
    "ex::U_ECALL",
    "ex::S_ECALL",
    "ex::__reserved__",
    "ex::M_ECALL",
    "ex::INSN_PG_FLT",
    "ex::LD_PG_FLT",
    "ex::__reserved__",
    "ex::ST_PG_FLT",
};

static const char *riscv_interrupts_text[] = {
    "irq::USER_SW",
    "irq::SUPERVISOR_SW",
    "irq::__reserved__",
    "irq::MACHINE_SW",
    "irq::USER_TIMER",
    "irq::SUPERVISOR_TIMER",
    "irq::__reserved__",
    "irq::MACHINE_TIMER",
    "irq::USER_EXTERN",
    "irq::SUPERVISOR_EXTERN",
    "irq::__reserved__",
    "irq::MACHINE_EXTERN",
};

void fi_set_logfile(const char *path) {
	if (path != NULL) {
		logfile = fopen(path, "w");
	};
}

void fi_log_header(void) {

    if (logfile == NULL) {
        logfile = stderr;
    }

    fprintf(logfile, "################################################################################\n");
    fprintf(logfile, "##                                                                            ##\n");
    fprintf(logfile, "##  QEMU fault-injection toolkit for RISC-V (riscv32gc) v0.1                  ##\n");
    fprintf(logfile, "##                                                                            ##\n");
    fprintf(logfile, "##  Copyright (c) 2019 Peer Adelt <peer.adelt@hni.uni-paderborn.de>           ##\n");
    fprintf(logfile, "##                                                                            ##\n");
    fprintf(logfile, "################################################################################\n#\n");
}

void fi_log_footer(void) {
	fprintf(logfile, "#   Mutation testing finished. Simulated %d mutants.\n", FEAR5_COUNT);
    fprintf(logfile, "#   TO DO: Footer with statistics and stuff like that...\n");

    if (logfile != stderr) {
        fflush(logfile);
        fclose(logfile);

        // fprintf(stderr, "\r Successfully finished mutation test... \n");
        int dIdx = (FEAR5_COUNT > 0) ? floor(log10((float) FEAR5_COUNT)) + 1 : 1;
        fprintf(stderr, "\r%0*d / %0*d (%03.02f %%)\n", dIdx, FEAR5_COUNT, dIdx, FEAR5_COUNT, 100.0f);
        fflush(stderr);
    }
}

void fi_log_goldenrun(uint64_t time, uint64_t time_max) {
	int dIdx = (FEAR5_COUNT > 0) ? floor(log10((float) FEAR5_COUNT)) + 1 : 1;
	int dTim = floor(log10((float) time_max)) + 1;

    fprintf(logfile, "#   Golden run took %"PRIu64" us to complete...\n", time);
    fprintf(logfile, "#    -> Mutants will timeout after %"PRIu64" us.\n#\n", time_max);
    fprintf(logfile, "#   TO DO: Display invocation parameters...\n#\n");
    fprintf(logfile, "#   Running %d mutants:\n", FEAR5_COUNT);
    fprintf(logfile, "#   [%*s, %22s, %*s]\n", dIdx, "ID", "TEST RESULT", (dTim + 3), "TIME US");
}

void fi_log_mutant(uint64_t time, uint64_t time_max, uint32_t code) {
	int dIdx = (FEAR5_COUNT > 0) ? floor(log10((float) FEAR5_COUNT)) + 1 : 1;
	int dTim = floor(log10((float) time_max)) + 1;

    const char *txt = "__unknown__";
    switch (code & 0xF00000) {
        case EXCEPTION:
            if ((code & 0xFFFFF) <= 0xF) {
                txt = riscv_exceptions_text[code & 0xF];
            } else {
                txt = base_exceptions_text[code & 0xF];
            }
            break;
        case MISSING_EXT:
            switch (code & 0xFFFFF) {
                case (1 << 0):
                    txt = "MISSING_EXT_RVA";
                    break;
                case (1 << 2):
                    txt = "MISSING_EXT_RVC";
                    break;
                case (1 << 3):
                    txt = "MISSING_EXT_RVD";
                    break;
                case (1 << 5):
                    txt = "MISSING_EXT_RVF";
                    break;
                case (1 << 12):
                    txt = "MISSING_EXT_RVM";
                    break;
            }
            break;
        default:
            if (code & 0x10000000) {
                uint32_t exception_code = code & 0x0FFFFFFF;
                if (code & 0x80000000) {
                    if (exception_code > 11) {
                        txt = "irq::__reserved__";
                    } else {
                        txt = riscv_interrupts_text[exception_code];
                    }
                } else {
                    if (exception_code > 15) {
                        txt = "ex::__reserved__";
                    } else {
                        txt = riscv_exceptions_text[exception_code];
                    }
                }
            } else {
                txt = mutant_result_text[code >> 20];
            }
            break;
    }

    fprintf(logfile, "     %0*d, %22s, %*"PRIu64" us\n",
            dIdx, FEAR5_CURRENT->id,
            txt,
            dTim, time);

    flush_div = (flush_div + 1) % FLUSH_DIV;
    if (flush_div == 0) {
        fflush(logfile);

        /* Display progress without too much slowdown... */
        if (logfile != stderr) {
            int i = FEAR5_INDEX + 1;
            float percent = (((float) i) / FEAR5_COUNT) * 100.0f;
            fprintf(stderr, "\r%0*d / %0*d (%03.02f %%)", dIdx, i, dIdx, FEAR5_COUNT, percent);
            fflush(stderr);
        }
    }
}
