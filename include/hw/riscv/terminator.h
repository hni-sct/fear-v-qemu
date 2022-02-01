/*
 * QEMU Test Termination Control Backend Interface
 *
 * Copyright (c) 2019-2020 Peer Adelt <peer.adelt@hni.uni-paderborn.de>
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

#ifndef HW_TERMINATOR_H
#define HW_TERMINATOR_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"

#define TYPE_TERMINATOR "terminator"
#define TERMINATOR_SIZE 4
#define TERMINATOR_ADDR_DEFAULT 0x10037004
#define TERMINATOR(obj) OBJECT_CHECK(Terminator, obj, TYPE_TERMINATOR)

enum {
	FI_EXITCODE_NORMAL  = 0x00000000,
	FI_EXITCODE_FAIL    = 0x00000001,
	FI_EXITCODE_TRAP    = 0x10000000,
	FI_REQUEST_IRQ      = 0xFFFFFFFF,
};

typedef struct Terminator
{
	/* <private> */
    // SysBusDevice parent_obj;
    DeviceState parent;

    /* <public> */
    MemoryRegion mmio;

	uint64_t address;
} Terminator;

#endif