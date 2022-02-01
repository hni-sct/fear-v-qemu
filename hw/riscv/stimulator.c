/*
 * QEMU Stimulator Backend
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

#include <math.h>
#include "hw/riscv/stimulator.h"
#include "qapi/visitor.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "fear5/faultinjection.h"

static Property stimulator_properties[] = {
    DEFINE_PROP_UINT64("address", StimulatorState, address, STIMULATOR_ADDR_DEFAULT),
    DEFINE_PROP_UINT64("seed", StimulatorState, seed, STIMULATOR_SEED_DEFAULT),
    DEFINE_PROP_UINT64("max_value", StimulatorState, max_value, STIMULATOR_MAX_VALUE_DEFAULT),
    DEFINE_PROP_END_OF_LIST(),
};

static uint64_t stimulator_read(void *opaque, hwaddr addr, unsigned int size)
{
    StimulatorState *s = opaque;
    uint64_t next;
    double dnext;

    switch(s->counter) {
        case 0:
            next = s->max_value;
            s->counter++;
            break;
        case 1:
            next = 0;
            s->counter++;
            break;
        default:
            drand48_r(&s->buffer, &dnext);
            next = (uint64_t) ((s->max_value + 1) * dnext);
            break;
    }

    return next;
}

static void stimulator_write(void *opaque, hwaddr addr,
           uint64_t val64, unsigned int size)
{
    
}

static const MemoryRegionOps stimulator_ops = {
    .read = stimulator_read,
    .write = stimulator_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4
    }
};

static void stimulator_realize(DeviceState *dev, Error **errp)
{
    StimulatorState *d = STIMULATOR(dev);
    srand48_r(d->seed, &d->buffer);
    memory_region_init_io(&d->mmio, NULL, &stimulator_ops, d,
                          TYPE_STIMULATOR, STIMULATOR_SIZE);
    MemoryRegion *sys_mem = get_system_memory();
    memory_region_add_subregion(sys_mem, d->address, &d->mmio);
}

static void stimulator_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);
	dc->desc = "Memory Stimulator Device";
    dc->realize = stimulator_realize;
    device_class_set_props(dc, stimulator_properties);
}

static const TypeInfo stimulator_info = 
{
    .name = TYPE_STIMULATOR,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(StimulatorState),
    .class_init = stimulator_class_init,
};

static void stimulator_register_type(void) 
{
    type_register_static(&stimulator_info);
}

type_init(stimulator_register_type);
