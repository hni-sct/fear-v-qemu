#include "fear5/faultinjection.h"
#include "fear5/logger.h"
#include "fear5/parser.h"
#include "sysemu/runstate.h"
#include <time.h>

Fear5State *f5;

TestSetup *setup;

void fear5_init(void) {
    fi_log_header();
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

	/* Check if golden run contains errors
       Note: "NOT_KILLED" is the exitcode without any known faulty behaviour. */
    if (f5->phase == GOLDEN_RUN && code != NOT_KILLED) {
        qemu_fi_exit(1, "ERROR: Golden Run has errors! Fix this or use another test program.");
    }

    if (FEAR5_COUNT == 0) {
        //qemu_fi_exit(0, "INFO:  Golden Run finished without errors. No mutant test -> closing QEMU.");
        qemu_fi_exit(0, NULL);
    }

    f5->next_code = code;

    qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);

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

float f5_get_timeout_factor(void)
{
    if (!setup || setup->timeout_factor < 1.0f) {
        fprintf(stderr, "INFO: Mutant timeout factor is set to default value 1.25f.\n");
        return 1.25f;
    }
    // fprintf(stderr, "INFO: Mutant timeout factor is set to %f!\n", setup->timeout_factor);
    return setup->timeout_factor;
}

uint64_t f5_get_timeout_us_extra(void)
{
    if (!setup || setup->timeout_us_extra == 0) {
        fprintf(stderr, "INFO: Mutant timeout extra wait time is set to default value 1000 us.\n");
        return 1000LL;
    }
    // fprintf(stderr, "INFO: Mutant timeout extra wait time is set to %ld!\n", setup->timeout_us_extra);
    return setup->timeout_us_extra;
}

// static QemuRecMutex f5_mutex;

// void f5_mutex_init(void) {
//     qemu_rec_mutex_init(&f5_mutex);
// }

// void f5_mutex_lock(void) {
//     qemu_rec_mutex_lock(&f5_mutex);
// }

// void f5_mutex_unlock(void) {
//     qemu_rec_mutex_unlock(&f5_mutex);
// }