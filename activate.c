/* 
 * Copyright (C) 2006, Intel Corporation
 * Copyright (C) 2012, Neil Horman <nhorman@tuxdriver.com> 
 * 
 * This file is part of irqbalance
 *
 * This program file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program in a file named COPYING; if not, write to the 
 * Free Software Foundation, Inc., 
 * 51 Franklin Street, Fifth Floor, 
 * Boston, MA 02110-1301 USA
 */

/* 
 * This file contains the code to communicate a selected distribution / mapping
 * of interrupts to the kernel.
 */
#include "config.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#include "irqbalance.h"

static int check_affinity(struct irq_info *info, cpumask_t applied_mask)
{
	cpumask_t current_mask;
	char buf[PATH_MAX];

	sprintf(buf, "/proc/irq/%i/smp_affinity", info->irq);
	if (process_one_line(buf, get_mask_from_bitmap, &current_mask) < 0)
		return 1;

	return cpus_equal(applied_mask, current_mask);
}

static void activate_mapping(struct irq_info *info, void *data __attribute__((unused)))
{
	char buf[PATH_MAX];
	FILE *file;
	int errsave, ret;
	cpumask_t applied_mask;

	/*
 	 * only activate mappings for irqs that have moved
 	 */
	if (!info->moved)
		return;

	if (!info->assigned_obj)
		return;

	/* activate only online cpus, otherwise writing to procfs returns EOVERFLOW */
	cpus_and(applied_mask, cpu_online_map, info->assigned_obj->mask);

	/*
 	 * Don't activate anything for which we have an invalid mask 
 	 */
	if (check_affinity(info, applied_mask)) {
		info->moved = 0; /* nothing to do, mark as done */
		return;
	}

	sprintf(buf, "/proc/irq/%i/smp_affinity", info->irq);
	file = fopen(buf, "w");
	errsave = errno;
	if (!file)
		goto error;

	cpumask_scnprintf(buf, PATH_MAX, applied_mask);
	ret = fprintf(file, "%s", buf);
	errsave = errno;
	fflush(file);
	if (fclose(file)) {
		errsave = errno;
		goto error;
	}
	if (ret < 0)
		goto error;
	info->moved = 0; /*migration is done*/
	return;
error:
	log(TO_ALL, LOG_WARNING,
		"Cannot change IRQ %i affinity: %s\n",
		info->irq, strerror(errsave));
	switch (errsave) {
	case ENOSPC: /* Specified CPU APIC is full. */
	case EAGAIN: /* Interrupted by signal. */
	case EBUSY: /* Affinity change already in progress. */
	case EINVAL: /* IRQ would be bound to no CPU. */
	case ERANGE: /* CPU in mask is offline. */
	case ENOMEM: /* Kernel cannot allocate CPU mask. */
		/* Do not blacklist the IRQ on transient errors. */
		break;
	default:
		/* Any other error is considered permanent. */
		info->level = BALANCE_NONE;
		info->moved = 0; /* migration impossible, mark as done */
		log(TO_ALL, LOG_WARNING, "IRQ %i affinity is now unmanaged\n",
			info->irq);
	}
}

void activate_mappings(void)
{
	for_each_irq(NULL, activate_mapping, NULL);
}
