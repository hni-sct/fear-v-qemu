/* This is the header for the fault injection config parser
   (c) 2019-2020 by Peer Adelt / Paderborn University */

#ifndef FAULTINJECTION_H_
#define FAULTINJECTION_H_

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "hw/core/cpu.h"
#include "exec/exec-all.h"
#include <inttypes.h>
#include <glib.h>

#define EXTRA_TIME 5000LL // 5ms
#define LEN_MAX 65536

//#define FEAR5_TIME_MEASUREMENT

enum MutantType {
    GPR_PERMANENT = 1,
    GPR_TRANSIENT = 2,
    CSR_PERMANENT = 3,
    CSR_TRANSIENT = 4,
    IMEM_PERMANENT = 5,
    IFR_PERMANENT = 7,
};

typedef struct Mutant {
    int id;
    int kind;
    uint64_t addr_reg_mem;
    uint64_t nr_access;
    uint64_t biterror;
} Mutant;

typedef struct TestSetup {
    GHashTable *monitors;
    GHashTable *stimulators;

    Mutant current;
    int m_index;
    int m_count;
} TestSetup;

typedef struct MemMonitor {
    const char *name;
    uint64_t address;
    unsigned int pos;
    uint64_t data[LEN_MAX];
    // TO DO: Other stuff like regex for matching, etc...
} MemMonitor;

typedef struct MemStimulator {
    const char *name;
    uint64_t address;
    unsigned int pos;
    FILE *file;
} MemStimulator;

enum MutationTestPhase {
    GOLDEN_RUN,
    MUTANT,
};

enum MutantResult {
    NOT_KILLED         = 0x000000,
    OUTPUT_DEVIATION   = 0x100000,
    TIMEOUT            = 0x200000,
    EXCEPTION          = 0x300000,
    UNKNOWN            = 0x400000,
    INTERRUPT          = 0x500000,
    MISSING_EXT        = 0x600000,
    EXIT_FAIL          = 0x700000,
    EXIT_TRAP          = 0x10000000,
};

typedef struct TbExecutionStatistics
{
    GSList *pc_list;
    uint64_t exec_counter;
    uint64_t icount;
} TbExecutionStatistics;

typedef struct MemAccessStatistics {
    uint64_t reads;
    uint64_t writes;
} MemAccessStatistics;

extern TestSetup *setup;
#define FEAR5_CURRENT ((setup && setup->m_index < setup->m_count) ? &(setup->current) : NULL)
#define FEAR5_COUNT   (setup ? setup->m_count : 0)
#define FEAR5_INDEX   (setup ? setup->m_index : 0)

extern enum MutationTestPhase phase;
extern uint64_t gpr_reads[32];
extern uint64_t gpr_writes[32];
extern uint64_t csr_reads[4096];
extern uint64_t csr_writes[4096];

extern GHashTable *mem_access;

extern GHashTable *fi_tb_stats;
extern GHashTable *fi_pc_executions;

/* config-parser.c */
//Mutant* mutant_current(void);
//int mutant_gotonext(void);
//int mutant_count(void);
//int mutant_index(void);

MemMonitor* fear5_get_monitor(uint64_t address);
MemStimulator* fear5_get_stimulator(uint64_t address);

/* controller.c */
void fear5_init(void);
void fear5_kill_mutant(uint32_t code);
//void qemu_fi_store_mutated_tb(TranslationBlock *tb);

void fear5_printtime(const char* prefix);

#endif /* FAULTINJECTION_H_ */
