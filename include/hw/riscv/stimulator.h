/*
 * QEMU Stimulator Backend Interface
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

#ifndef HW_STIMULATOR_H
#define HW_STIMULATOR_H

#include <stdlib.h>
#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"

#define TYPE_STIMULATOR "stimulator"
#define STIMULATOR_SIZE 10
#define STIMULATOR_ADDR_DEFAULT 0x10037008
#define STIMULATOR_SEED_DEFAULT 1
#define STIMULATOR_MAX_VALUE_DEFAULT 0xFFFFFFFF
#define STIMULATOR(obj) OBJECT_CHECK(StimulatorState, obj, TYPE_STIMULATOR)

typedef struct StimulatorState
{
	/* <private> */
    // SysBusDevice parent_obj;
    DeviceState parent;

    /* <public> */
    MemoryRegion mmio;

	uint64_t address;
    uint64_t seed;
    uint64_t max_value;
    int counter;
} StimulatorState;

#endif