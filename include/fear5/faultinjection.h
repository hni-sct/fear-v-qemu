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

#define EXTRA_TIME 1000LL // 1ms
#define LEN_MAX 65536

//#define FEAR5_TIME_MEASUREMENT

enum Fear5TestPhase {
    GOLDEN_RUN = 0,
    MUTANT = 1,
};

typedef struct Fear5TbExecCounter {
    GList *pcs;
    uint64_t x;
} Fear5TbExecCounter;

typedef struct Fear5ReadWriteCounter {
    uint64_t r;
    uint64_t w;
} Fear5ReadWriteCounter;

typedef struct Fear5State {
    enum Fear5TestPhase phase;
    Fear5ReadWriteCounter gpr[32];
    Fear5ReadWriteCounter csr[4096];
    GHashTable *mem;
    GHashTable *tb;
} Fear5State;

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
} MemMonitor;

typedef struct MemStimulator {
    const char *name;
    uint64_t address;
    unsigned int pos;
    FILE *file;
} MemStimulator;

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

#define FEAR5_CURRENT ((setup && setup->m_index < setup->m_count) ? &(setup->current) : NULL)
#define FEAR5_COUNT   (setup ? setup->m_count : 0)
#define FEAR5_INDEX   (setup ? setup->m_index : 0)

extern Fear5State *f5;

extern TestSetup *setup;

MemMonitor* fear5_get_monitor(uint64_t address);
MemStimulator* fear5_get_stimulator(uint64_t address);
void fear5_init(void);
void fi_reset_state(void);
void fear5_kill_mutant(uint32_t code);
void fear5_printtime(const char* prefix);

#endif /* FAULTINJECTION_H_ */
