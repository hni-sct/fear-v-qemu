#include "fear5/faultinjection.h"
#include "fear5/logger.h"
#include "fear5/parser.h"
#include "sysemu/runstate.h"
//#include "hw/boards.h"
#include <time.h>

TestSetup *setup;
enum MutationTestPhase phase = GOLDEN_RUN;

static QEMUTimer *timer;
static int64_t tStart;
static uint64_t runTimeMax;

uint64_t gpr_reads[32];
uint64_t gpr_writes[32];
uint64_t csr_reads[4096];
uint64_t csr_writes[4096];

GHashTable *mem_access;

GHashTable *fi_tb_stats;
GHashTable *fi_pc_executions;

static void timeout(void *opaque)
{
    CPUState *cpu;
    timer_del(timer);
    /* we're call this from the IO-thread. If
       we reset the cpu from here, we will crash.
       So interrupt the running cpu thread hand do
       the reset there.*/
    CPU_FOREACH(cpu) {
        // TODO: Prüfen, ob das nicht generell irgendwie besser geht...
        //       Auf lange Sicht wäre es schon cool, gar nicht mehr ins Target zu müssen ab hier.
        // NOTE: Dieses Interrupt-Flag bewirkt auch was bei ARM.
        //       (Aber wie oben geschrieben: am besten weg hiermit. Das fällt mir noch auf die Füße.)
        cpu_interrupt(cpu, CPU_INTERRUPT_FEAR5_TIMEOUT);
        //fear5_kill_mutant(TIMEOUT);
    }
}

static void fi_reset_state(void) {
    //qemu_log_mask(FEAR5_LOG_GOLDENRUN, "Clearing 'fi_tb_stats'...\n");
    g_hash_table_remove_all(fi_tb_stats);
    g_hash_table_remove_all(fi_pc_executions);

    g_hash_table_remove_all(mem_access);

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

    memset(gpr_reads, 0, 32*sizeof(uint64_t));
    memset(gpr_writes, 0, 32*sizeof(uint64_t));
    memset(csr_reads, 0, 4096*sizeof(uint64_t));
    memset(csr_writes, 0, 4096*sizeof(uint64_t));
    tStart = qemu_clock_get_us(QEMU_CLOCK_VIRTUAL);
}

void fear5_init(void) {
    fi_log_header();
    fi_reset_state();
    timer = timer_new_us(QEMU_CLOCK_VIRTUAL, timeout, NULL);
}

static void iterate_tb_pcs(gpointer item, gpointer parent) {
    // An dieser Stelle nichts ausgeben und stattdessen
    // die PC->ExecCounter Hashmap befuellen.
    target_ulong *pc = (target_ulong *)item;
    uint64_t *to_add = (uint64_t *)parent;

    uint64_t *counter = g_hash_table_lookup(fi_pc_executions, GUINT_TO_POINTER(*pc));
    if (counter == NULL) {
        counter = g_new0(uint64_t, 1);
        g_hash_table_insert(fi_pc_executions, GUINT_TO_POINTER(*pc), counter);
    }
    *counter += *to_add;
}

static void fi_output_tb_stats(gpointer key, gpointer value, gpointer user_data) {
    target_ulong tb_pc = GPOINTER_TO_UINT(key);
    TbExecutionStatistics *stats = g_hash_table_lookup(fi_tb_stats, GUINT_TO_POINTER(tb_pc));
    if (stats) {
        if (stats->pc_list) {
            g_slist_foreach(stats->pc_list, (GFunc)iterate_tb_pcs, &stats->exec_counter);
        }
    }
}

static gint my_comparator(gconstpointer item1, gconstpointer item2) {
    if (item1 < item2)
        return -1;
    if (item1 > item2)
        return 1;
    return 0;
}

static void qemu_fi_exit(int i, const char *t) {

    /* TO DO: Check if this is still required! */
    if (qemu_loglevel_mask(FEAR5_LOG_GOLDENRUN)) {
        /* OLD: GPR Accesses... */
        qemu_log("\nGPR Read Access Statistics:\n");
        qemu_log_mask(FEAR5_LOG_GOLDENRUN, "--------------------------------------------------------------------------------\n");
        for (int i = 1; i < 32; i++) {
            if (gpr_reads[i]) {
                qemu_log("GPR[%d]: %" PRIu64 "\n", i, gpr_reads[i]);
            }
        }

        qemu_log("\nCSR Read Access Statistics:\n");
        qemu_log_mask(FEAR5_LOG_GOLDENRUN, "--------------------------------------------------------------------------------\n");
        for (int i = 0; i < 4096; i++) {
            if (csr_reads[i]) {
                qemu_log("CSR[%d]: %" PRIu64 "\n", i, csr_reads[i]);
            }
        }
    }

    /* Compact golden run statistics... */
    if (qemu_loglevel_mask(FEAR5_LOG_GOLDENRUN)) {
        /* Output GPR Accesses (R/W/Total) */
        qemu_log_mask(FEAR5_LOG_GOLDENRUN, "\nGPR executions <#reads, #writes, #total>:\n");
        qemu_log_mask(FEAR5_LOG_GOLDENRUN, "--------------------------------------------------------------------------------\n");
        for (int i = 1; i < 32; i++) {
            qemu_log_mask(FEAR5_LOG_GOLDENRUN, "GPR[%d]:%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n", i, gpr_reads[i], gpr_writes[i], gpr_reads[i] + gpr_writes[i]);
        }

        /* Output CSR Accesses (R/W/Total) */
        qemu_log_mask(FEAR5_LOG_GOLDENRUN, "\nCSR executions <#reads, #writes, #total>:\n");
        qemu_log_mask(FEAR5_LOG_GOLDENRUN, "--------------------------------------------------------------------------------\n");
        for (int i = 0; i < 4096; i++) {
            /* Skip reporting about any CSR without accesses */
            uint64_t a = csr_reads[i] + csr_writes[i];
            if (a) {
                qemu_log_mask(FEAR5_LOG_GOLDENRUN, "CSR[%d]:%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n", i, csr_reads[i], csr_writes[i], a);
            }
        }

        /* Output TB_INSN_EXEC stats... */
        // 1) foreach k,v in <fi_tb_stats>
        /* TO DO: Check, if this is functional. I can see no output from this!
                  This seems also to be handled by Output PC_EXEC_SUMMARY below... */
        g_hash_table_foreach(fi_tb_stats, fi_output_tb_stats, NULL);

        /* Output PC_EXEC_SUMMARY */
        qemu_log_mask(FEAR5_LOG_GOLDENRUN, "\nINSTRUCTION executions:\n");
        qemu_log_mask(FEAR5_LOG_GOLDENRUN, "--------------------------------------------------------------------------------\n");
        GList *k = g_list_sort(g_hash_table_get_keys(fi_pc_executions), (GCompareFunc)my_comparator);
        while(k) {
            // printf("KEY: 0x" TARGET_FMT_lx "\n", GPOINTER_TO_UINT(k->data));
            //qemu_log("0x" TARGET_FMT_lx ", %" PRIu64 "\n", )
            uint64_t *counter = g_hash_table_lookup(fi_pc_executions, k->data);
            if (counter == NULL) {
                printf("ERROR2: counter == NULL!\n");
                exit(1);
            }
            qemu_log_mask(FEAR5_LOG_GOLDENRUN, "EXE[" TARGET_FMT_lx "]:%" PRIu64 "\n", GPOINTER_TO_UINT(k->data), *counter);

            k = k->next;
        }

        /* Output MEM Accesses (R/W/Total) */
        qemu_log_mask(FEAR5_LOG_GOLDENRUN, "\nMemory executions <#reads, #writes, #total>:\n");
        qemu_log_mask(FEAR5_LOG_GOLDENRUN, "--------------------------------------------------------------------------------\n");
        GList *k2 = g_list_sort(g_hash_table_get_keys(mem_access), (GCompareFunc)my_comparator);
        while(k2) {
            MemAccessStatistics *mem = g_hash_table_lookup(mem_access, k2->data);
            qemu_log_mask(FEAR5_LOG_GOLDENRUN, "MEMORY[" TARGET_FMT_lx "]:%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n", GPOINTER_TO_UINT(k2->data), mem->reads, mem->writes, (mem->reads + mem->writes));
            k2 = k2->next;
        }

        qemu_log_mask(FEAR5_LOG_GOLDENRUN, "--------------------------------------------------------------------------------\n");
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
    if (phase == GOLDEN_RUN && code != NOT_KILLED) {
        qemu_fi_exit(1, "ERROR: Golden Run has errors! Fix this or use another test program.");
    }

    if (FEAR5_COUNT == 0) {
        //qemu_fi_exit(0, "INFO:  Golden Run finished without errors. No mutant test -> closing QEMU.");
        qemu_fi_exit(0, NULL);
    }

    int64_t tEnd = qemu_clock_get_us(QEMU_CLOCK_VIRTUAL);
    uint64_t runTime = (tEnd < tStart) ? (-tStart-tEnd) : (tEnd-tStart);

    if (phase == GOLDEN_RUN) {
        runTimeMax = (1.5f * runTime) + EXTRA_TIME;
        fi_log_goldenrun(runTime, runTimeMax);
    } else if (phase == MUTANT) {
        fi_log_mutant(runTime, runTimeMax, code);
    }

    // if (mutant_gotonext() == 1) {

    // Try to select the next mutant...
    if (!FEAR5_COUNT || fear5_gotonext_mutant()) {
        // Quit QEMU if no further mutants available
        fi_log_footer();
        qemu_fi_exit(0, NULL);
    }

    // Reset all CPUs
    CPUState *cpu;
    CPU_FOREACH(cpu) {

//    	RISCVCPU *rvcpu = RISCV_CPU(cpu);
//    	resettable_assert_reset(rvcpu, RESET_TYPE_COLD);

        // NEW:
        //if (phase == GOLDEN_RUN) {
            // Invalidate all tbs after golden run so the first mutant
            // can retranslate and inject faults:
            tb_flush(cpu);
            //tlb_flush(cpu);
            phase = MUTANT;
        //} else if (phase == MUTANT) {
            // Invalidate all tbs into which faults have been injected
            // and keep the rest (yields significant speedup):
        //    for (int i = 0; i < g_slist_length(tb_list); i++) {
        //        tb_phys_invalidate(g_slist_nth_data(tb_list, i), -1);
        //    }
        //    g_slist_free(tb_list);
        //    tb_list = NULL;
        //}

        // Reset all mutant and CPU state
        // cpu_reset(cpu);
        //cpu_exit(cpu);
        //qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);

        //qemu_system_reset(SHUTDOWN_CAUSE_NONE); // as in qemu_init() -> l. 4500
        qemu_system_reset(SHUTDOWN_CAUSE_GUEST_RESET);

        fi_reset_state();
        
        // Rearm timeout alarm
        timer_mod(timer, tStart + runTimeMax);

//        resettable_release_reset(rvcpu, RESET_TYPE_COLD);

        // cpu_loop_exit(cpu);
    }

}

//void qemu_fi_store_mutated_tb(TranslationBlock *tb) {
//	TranslationBlock *tBlock = tb;
//    tb_list = g_slist_append(tb_list, tBlock);
//}

//Mutant* mutant_current(void)
//{
//    if (setup && setup->m_index == -1) {
//        return NULL;
//    }
//    if (setup && setup->m_index < setup->m_count) {
//        return &(setup->current);
//    }
//    return NULL;
//}

//int mutant_gotonext(void)
//{
//    if (!FEAR5_COUNT) {
//        return 1;
//    }
//    return fear5_gotonext_mutant();
//}

//int mutant_count(void)
//{
//    if (!setup)
//        return 0;
//    return setup->m_count;
//}
//
//int mutant_index(void)
//{
//    if (!setup)
//        return 0;
//    return setup->m_index;
//}

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
