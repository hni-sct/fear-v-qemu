#include "fear5/faultinjection.h"
#include "fear5/logger.h"
#include "fear5/parser.h"
#include "sysemu/runstate.h"
#include <time.h>

Fear5State *f5;

TestSetup *setup;

static QEMUTimer *timer = NULL;
static int64_t tStart;
static uint64_t runTimeMax;

static void timeout(void *opaque)
{
    fear5_kill_mutant(TIMEOUT);
}

void fi_reset_state(void) {
    memset(f5->gpr, 0, 32*sizeof(Fear5ReadWriteCounter));
    memset(f5->csr, 0, 4096*sizeof(Fear5ReadWriteCounter));
    g_hash_table_remove_all(f5->mem);
    g_hash_table_remove_all(f5->tb);

    //    qemu_fi_monitors_reset();
    if (setup && setup->monitors) {
        GList *values = g_hash_table_get_values(setup->monitors);
        for (int i = 0; i < g_list_length(values); i++) {
            MemMonitor *m = g_list_nth_data(values, i);
            m->pos = 0;
        }
    }

    //    qemu_fi_stimulators_reset();
    if (setup && setup->stimulators) {
        GList *values = g_hash_table_get_values(setup->stimulators);
        for (int i = 0; i < g_list_length(values); i++) {
            MemStimulator *s = g_list_nth_data(values, i);
            s->pos = 0;
        }
    }

    tStart = qemu_clock_get_us(QEMU_CLOCK_VIRTUAL);

    if (timer && f5->phase == MUTANT) {
        timer_mod(timer, tStart + runTimeMax);
    }
}

void fear5_init(void) {
    fi_log_header();
    timer = timer_new_us(QEMU_CLOCK_VIRTUAL, timeout, NULL);
}

static void get_pc_exe(gpointer item, gpointer user) {
    Fear5TbExecCounter *tbe = (Fear5TbExecCounter *) item;
    GHashTable *pc_exe = (GHashTable *) user;

    // Calculate PC executions from TB executions...
    for (int i = 0; i < g_list_length(tbe->pcs); i++) {
        target_ulong *pc = g_list_nth_data(tbe->pcs, i);
        uint64_t *ctr = g_hash_table_lookup(pc_exe, GUINT_TO_POINTER(*pc));
        if (ctr == NULL) {
            ctr = g_new0(uint64_t, 1);
            g_hash_table_insert(pc_exe, GUINT_TO_POINTER(*pc), ctr);
        }
        *ctr += tbe->x;
    }
}

static gint compare(gconstpointer item1, gconstpointer item2) {
    if (item1 < item2)
        return -1;
    if (item1 > item2)
        return 1;
    return 0;
}

static void qemu_fi_exit(int i, const char *t) {

    /* Compact golden run statistics... */
    if (qemu_loglevel_mask(FEAR5_LOG_GOLDENRUN)) {
        /* Output GPR Accesses (R/W/Total) */
        qemu_log("\nGPR executions <#reads, #writes, #total>:\n");
        qemu_log("--------------------------------------------------------------------------------\n");
        for (int i = 1; i < 32; i++) {
            qemu_log("GPR[%d]:%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n", i, f5->gpr[i].r, f5->gpr[i].w, f5->gpr[i].r + f5->gpr[i].w);
        }

        /* Output CSR Accesses (R/W/Total) */
        qemu_log("\nCSR executions <#reads, #writes, #total>:\n");
        qemu_log("--------------------------------------------------------------------------------\n");
        for (int i = 0; i < 4096; i++) {
            /* Skip reporting about any CSR without accesses */
            uint64_t a = f5->csr[i].r + f5->csr[i].w;
            if (a) {
                qemu_log("CSR[%d]:%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n", i, f5->csr[i].r, f5->csr[i].w, a);
            }
        }

        /* Calculate and output PC exec stats... */
        GHashTable *pc_exe = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
        g_list_foreach(g_hash_table_get_values(f5->tb), get_pc_exe, pc_exe);

        /* Output PC_EXEC_SUMMARY */
        qemu_log("\nINSTRUCTION executions:\n");
        qemu_log("--------------------------------------------------------------------------------\n");
        GList *k = g_list_sort(g_hash_table_get_keys(pc_exe), compare);
        while(k) {
            uint64_t *counter = g_hash_table_lookup(pc_exe, k->data);
            qemu_log("EXE[" TARGET_FMT_lx "]:%" PRIu64 "\n", GPOINTER_TO_UINT(k->data), *counter);
            k = k->next;
        }
        g_hash_table_destroy(pc_exe);

        /* Output MEM Accesses (R/W/Total) */
        qemu_log("\nMemory executions <#reads, #writes, #total>:\n");
        qemu_log("--------------------------------------------------------------------------------\n");
        GList *k2 = g_list_sort(g_hash_table_get_keys(f5->mem), compare);
        while(k2) {
            Fear5ReadWriteCounter *mem = g_hash_table_lookup(f5->mem, k2->data);
            qemu_log("MEMORY[" TARGET_FMT_lx "]:%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n", GPOINTER_TO_UINT(k2->data), mem->r, mem->w, (mem->r + mem->w));
            k2 = k2->next;
        }

        qemu_log("--------------------------------------------------------------------------------\n");
    }

    if (t != NULL) {
        printf("%s\n", t);
    }
    mutantlist_close();
#ifdef FEAR5_TIME_MEASUREMENT
    fear5_printtime("qemu_fi_exit");
#endif
    exit(i);
}

void fear5_kill_mutant(uint32_t code) {

	if (timer) {
		timer_del(timer);
	}

    /* Check if golden run contains errors
       Note: "NOT_KILLED" is the exitcode without any known faulty behaviour. */
    if (f5->phase == GOLDEN_RUN && code != NOT_KILLED) {
        qemu_fi_exit(1, "ERROR: Golden Run has errors! Fix this or use another test program.");
    }

    if (FEAR5_COUNT == 0) {
        //qemu_fi_exit(0, "INFO:  Golden Run finished without errors. No mutant test -> closing QEMU.");
        qemu_fi_exit(0, NULL);
    }

    int64_t tEnd = qemu_clock_get_us(QEMU_CLOCK_VIRTUAL);
    uint64_t runTime = (tEnd < tStart) ? (-tStart-tEnd) : (tEnd-tStart);

    if (f5->phase == GOLDEN_RUN) {
        runTimeMax = (1.125f * runTime) + EXTRA_TIME;
        fi_log_goldenrun(runTime, runTimeMax);
    } else if (f5->phase == MUTANT) {
        fi_log_mutant(runTime, runTimeMax, code);
    }

    // Try to select the next mutant...
    if (!FEAR5_COUNT || fear5_gotonext_mutant()) {
        // Quit QEMU if no further mutants available
        fi_log_footer();
        qemu_fi_exit(0, NULL);
    }

    // Reset all CPUs
    CPUState *cpu;
    CPU_FOREACH(cpu) {

        // Open question: should the TB flush be done here or better during
        //                the reset of the terminator device?
        tb_flush(cpu);
        // Same here: should we update the phase here or during QOM reset?
        f5->phase = MUTANT;
        
        // Request reset: this has to be asynchronous to make the timeout work properly!
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }

}


MemMonitor* fear5_get_monitor(uint64_t address)
{
    if (!setup || !setup->monitors) {
        return NULL;
    }
    return (MemMonitor *) g_hash_table_lookup(setup->monitors, GINT_TO_POINTER(address));
}

MemStimulator* fear5_get_stimulator(uint64_t address)
{
    if (!setup || !setup->stimulators) {
        return NULL;
    }
    return (MemStimulator *) g_hash_table_lookup(setup->stimulators, GINT_TO_POINTER(address));
}

void fear5_printtime(const char* prefix)
{
    struct timespec time;
    int t = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time);
    if (t) {
        printf("Error getting CPU time! Exiting!\n");
        exit(1);
    }
    printf("TIME %s, %ld sec, %ld nsec\n", prefix, time.tv_sec, time.tv_nsec);
}
