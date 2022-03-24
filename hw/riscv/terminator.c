/*
 * QEMU Test Termination Control Backend
 *
 * Copyright (c) 2019-2020 Peer Adelt <peer.adelt@hni.uni-paderborn.de>
 *
 * - TO DO: Proper description...
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/riscv/terminator.h"
#include "qapi/visitor.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "exec/ram_addr.h"
#include "fear5/faultinjection.h"
#include "fear5/logger.h"
#include "fear5/parser.h"

static QEMUTimer *timer = NULL;
static int64_t tStart;
static uint64_t runTimeMax;

static void timeout(void *opaque)
{
    fear5_kill_mutant(TIMEOUT);
}

static Property terminator_properties[] = {
    DEFINE_PROP_UINT64("address", Terminator, address, TERMINATOR_ADDR_DEFAULT),
    DEFINE_PROP_END_OF_LIST(),
};

// static QEMUTimer *timer;
// static int64_t tStart;

// static void timeout(void *opaque)
// {
//     CPUState *cpu;
//     CPU_FOREACH(cpu) {
//         cpu_interrupt(cpu, CPU_INTERRUPT_HARD);
//         //cpu->exception_index = -1;
//         cpu->halted = 0;
//         fprintf(stderr, "INFO: Interrupted CPU for command 'FI_REQUEST_IRQ'...\n");
//     }
// }

// static void request_irq(void) {
//     fprintf(stderr, "INFO: Requesting IRQ in 1 sec...\n");
//     timer = timer_new_us(QEMU_CLOCK_HOST, timeout, NULL);
//     tStart = qemu_clock_get_us(QEMU_CLOCK_HOST);
//     timer_mod(timer, tStart + 1000000LL);
// }

static uint64_t terminator_read(void *opaque, hwaddr addr, unsigned int size)
{
    return 0;
}

static void terminator_write(void *opaque, hwaddr addr,
           uint64_t val64, unsigned int size)
{
    if (addr == (4)) {
        fprintf(stderr, "%c", ((uint32_t) (val64 & 0xff)));
        return;
    }

    uint32_t exitcode = val64 & 0xffffffff;

    switch (exitcode) {
    case FI_EXITCODE_NORMAL:
        return fear5_kill_mutant(NOT_KILLED);
    case FI_EXITCODE_FAIL:
        return fear5_kill_mutant(EXIT_FAIL);
    default:
        if (exitcode & FI_EXITCODE_TRAP) {
            return fear5_kill_mutant(exitcode);
        }
    // case FI_EXITCODE_EXCEPTION:
    //     return fear5_kill_mutant(EXCEPTION);
    // case FI_REQUEST_IRQ:
    //     return request_irq();
    // Ignore unknown exit codes!
    // default:
    //     if (exitcode & FI_EXITCODE_TRAP) {
    //         return fear5_kill_mutant(exitcode);
    //     } else {
    //         return fear5_kill_mutant(UNKNOWN);
    //     }
    }
}

static const MemoryRegionOps terminator_ops = {
    .read = terminator_read,
    .write = terminator_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void terminator_realize(DeviceState *dev, Error **errp)
{
    Terminator *d = TERMINATOR(dev);
    memory_region_init_io(&d->mmio, NULL, &terminator_ops, d,
                          TYPE_TERMINATOR, TERMINATOR_SIZE);
    MemoryRegion *sys_mem = get_system_memory();
    memory_region_add_subregion(sys_mem, d->address, &d->mmio);

    // fi_log_header();
    timer = timer_new_us(QEMU_CLOCK_VIRTUAL, timeout, NULL);
    // f5_mutex_init();
}

static void terminator_reset_enter(Object *obj, ResetType type)
{
    // Terminator *s = TERMINATOR(obj);
    // printf("DONE: TERMINATOR RESET ENTER...\n");
}

static void terminator_reset_hold(Object *obj)
{
    // Terminator *s = TERMINATOR(obj);
    // printf("DONE: TERMINATOR RESET HOLD...\n");
}

static void terminator_reset_exit(Object *obj)
{
    //qemu_rec_mutex_init(m);

    // Terminator *s = TERMINATOR(obj);
    // printf("DONE: TERMINATOR RESET EXIT...\n");
    // fi_reset_state();

    // Delete timer
    if (timer) {
        timer_del(timer);
    }

    // Log current state
    int64_t tEnd = qemu_clock_get_us(QEMU_CLOCK_VIRTUAL);
    uint64_t runTime = (tEnd < tStart) ? (-tStart-tEnd) : (tEnd-tStart);

    if (f5->phase == GOLDEN_RUN) {
        runTimeMax = (f5_get_timeout_factor() * runTime) + f5_get_timeout_us_extra();
        fi_log_goldenrun(runTime, runTimeMax);
        f5->phase = MUTANT;
    } else if (f5->phase == MUTANT) {
        fi_log_mutant(runTime, runTimeMax, f5->next_code);
    }

    // Clear state
    memset(f5->gpr, 0, 32*sizeof(Fear5ReadWriteCounter));
    memset(f5->csr, 0, 4096*sizeof(Fear5ReadWriteCounter));
    // f5_mutex_lock();
    g_hash_table_remove_all(f5->mem8);
    g_hash_table_remove_all(f5->mem16);
    g_hash_table_remove_all(f5->mem32);
    // f5_mutex_unlock();
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

    if (f5->phase == PRE_INIT) {
        f5->phase = GOLDEN_RUN;
        return;
    }

    tStart = qemu_clock_get_us(QEMU_CLOCK_VIRTUAL);

    if (timer && f5->phase == MUTANT) {
        timer_mod(timer, tStart + runTimeMax);
    }

    // Reset all CPUs
    CPUState *cpu;
    CPU_FOREACH(cpu) {

        // Open question: should the TB flush be done here or better during
        //                the reset of the terminator device?
        tb_flush(cpu);
        // Same here: should we update the phase here or during QOM reset?
        // f5->phase = MUTANT;
    }

    // Mutant *m;
    // m = FEAR5_CURRENT;
    // if (m) {
    //     // Minimal TB Invalidation: reset, what has been mutated by CURRENT(!) mutant
    //     if (m->kind == IMEM_PERMANENT || m->kind == IMEM_STUCK_AT_ZERO || m->kind == IMEM_STUCK_AT_ONE) {
    //         tb_invalidate_phys_range(m->addr_reg_mem, m->addr_reg_mem + 1);
    //     }
    // }

    // Try to select the next mutant...
    if (!FEAR5_COUNT || fear5_gotonext_mutant()) {
        // Quit QEMU if no further mutants available
        fi_log_footer();
        exit(0);
    }

    // m = FEAR5_CURRENT;
    // if (m) {
    //     // Minimal TB Invalidation: reset, what is about to be mutated by NEXT mutant
    //     if (m->kind == IMEM_PERMANENT || m->kind == IMEM_STUCK_AT_ZERO || m->kind == IMEM_STUCK_AT_ONE) {
    //         tb_invalidate_phys_range(m->addr_reg_mem, m->addr_reg_mem + 1);
    //     }
    // }
}

static void terminator_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);
	dc->desc = "Fault-Injection Backend Device";
    dc->realize = terminator_realize;
    dc->user_creatable = true;
    device_class_set_props(dc, terminator_properties);

    ResettableClass *rc = RESETTABLE_CLASS(klass);
    rc->phases.enter = terminator_reset_enter;
    rc->phases.hold  = terminator_reset_hold;
    rc->phases.exit  = terminator_reset_exit;
}

static const TypeInfo terminator_info = 
{
    .name = TYPE_TERMINATOR,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Terminator),
    .class_init = terminator_class_init,
};

static void terminator_register_type(void) 
{
    type_register_static(&terminator_info);
}

type_init(terminator_register_type);
