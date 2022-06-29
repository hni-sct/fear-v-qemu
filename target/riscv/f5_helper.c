/*
 * FEAR5 Helpers for QEMU.
 *
 * Copyright (c) 2022 Peer Adelt, peer.adelt@hni.upb.de
 * Copyright (c) 2019-2022 Paderborn University, DE
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

#include "qemu/osdep.h"
#include "cpu.h"
#include "qemu/main-loop.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "fear5/faultinjection.h"

void helper_f5_trace_gpr_read(target_ulong idx)
{
    f5->gpr[idx].r++;
}

void helper_f5_trace_gpr_write(target_ulong idx)
{
    f5->gpr[idx].w++;
}

target_ulong helper_f5_mutate_gpr(target_ulong idx, target_ulong reg)
{
    Mutant* m = FEAR5_CURRENT;
    if (m && m->addr_reg_mem == idx) {
        if (m->kind == GPR_PERMANENT || (m->kind == GPR_TRANSIENT && m->nr_access == (f5->gpr[idx].r + f5->gpr[idx].w))) {
            reg ^= m->biterror;
        } else if (m->kind == GPR_STUCK_AT_ZERO) {
            reg &= ~(m->biterror);
        } else if (m->kind == GPR_STUCK_AT_ONE) {
            reg |= m->biterror;
        }
    }
    return reg;
}

static inline GHashTable *get_memx_htable(MemOp op)
{
    switch (op & MO_SIZE) {
        case MO_8:
            return f5->mem8;
        case MO_16:
            return f5->mem16;
        case MO_32:
            return f5->mem32;
    }
    g_assert_not_reached();
    return NULL;
}

static inline Fear5ReadWriteCounter *get_memx_counter_from_htable(GHashTable *ht, target_ulong addr)
{
    Fear5ReadWriteCounter *ctr = g_hash_table_lookup(ht, GUINT_TO_POINTER(addr));
    if (ctr == NULL) {
        ctr = g_new0(Fear5ReadWriteCounter, 1);
        g_hash_table_insert(ht, GUINT_TO_POINTER(addr), ctr);
    }
    return ctr;
}

void helper_f5_trace_load(target_ulong address, target_ulong mop)
{
    /* Split-up tracing by Memory Operation size */
    Fear5ReadWriteCounter *mem = get_memx_counter_from_htable(get_memx_htable(mop), address);
    mem->r++;
}

void helper_f5_trace_store(target_ulong address, target_ulong mop)
{
    /* Split-up tracing by Memory Operation size */
    Fear5ReadWriteCounter *mem = get_memx_counter_from_htable(get_memx_htable(mop), address);
    mem->w++;
}

target_ulong helper_f5_mutate_memop(target_ulong reg, target_ulong address, target_ulong mop)
{
    Mutant* m = FEAR5_CURRENT;
    // if (m && (m->kind == DMEM_PERMANENT || m->kind == DMEM_STUCK_AT_ZERO || m->kind == DMEM_STUCK_AT_ONE)) {

        unsigned s = memop_size(mop);
        
        if (unlikely(m->addr_reg_mem >= address && m->addr_reg_mem < address + s)) {
            target_ulong e = m->biterror;
            unsigned offset = m->addr_reg_mem - address;
            switch(offset) {
                case 1:
                    e <<= 8;
                    break;
                case 2:
                    e <<= 16;
                    break;
                case 3:
                    e <<= 24;
                    break;
            }

            switch(m->kind) {
                case DMEM_PERMANENT:
                    return reg ^= e;
                case DMEM_STUCK_AT_ZERO:
                    return reg &= ~e;
                case DMEM_STUCK_AT_ONE:
                    return reg |= e;
            }
        }
    // }
    // Do not mutate otherwise
    return reg;
}

// void helper_f5_trace_mem_filter(target_ulong idx, target_ulong base, target_ulong offset)
// {
//     /* Records the address-containing GPRs of any Load/Store instruction */
//     // if (qemu_loglevel_mask(FEAR5_LOG_GOLDENRUN)) {
//     //     qemu_log("LD/ST for GPR %u (Access %" PRIu64 "): [" TARGET_FMT_lx " + %d]\n", idx, f5->gpr[idx].r, base, (int) offset);
//     // }
// }

void helper_f5_trace_tb_exec(target_ulong pc)
{
    /* Increment execution counter for this TB... */
    Fear5TbExecCounter *stats = g_hash_table_lookup(f5->tb, GUINT_TO_POINTER(pc));
    if (stats == NULL) {
        // This should not happen!
        fprintf(stderr, "ERROR: cannot find Fear5TbExecCounter struct!\n");
        exit(1);
    }
    stats->x++;
}