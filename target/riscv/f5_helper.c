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

void helper_f5_trace_load(target_ulong address)
{
    /* Update load counter */
    Fear5ReadWriteCounter *mem = g_hash_table_lookup(f5->mem, GUINT_TO_POINTER(address));
    if (mem == NULL) {
        mem = g_new0(Fear5ReadWriteCounter, 1);
        g_hash_table_insert(f5->mem, GUINT_TO_POINTER(address), mem);
    }
    mem->r++;
}

void helper_f5_trace_store(target_ulong address)
{
    /* Update store counter */
    Fear5ReadWriteCounter *mem = g_hash_table_lookup(f5->mem, GUINT_TO_POINTER(address));
    if (mem == NULL) {
        mem = g_new0(Fear5ReadWriteCounter, 1);
        g_hash_table_insert(f5->mem, GUINT_TO_POINTER(address), mem);
    }
    mem->w++;
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