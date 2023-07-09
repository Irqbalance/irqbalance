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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

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
	int ret = 0;
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
	if (check_affinity(info, applied_mask))
		return;

	sprintf(buf, "/proc/irq/%i/smp_affinity", info->irq);
	file = fopen(buf, "w");
	if (!file)
		return;

	cpumask_scnprintf(buf, PATH_MAX, applied_mask);
	ret = fprintf(file, "%s", buf);
	if (ret < 0) {
		log(TO_ALL, LOG_WARNING, "cannot change irq %i's affinity, add it to banned list", info->irq);
		add_banned_irq(info->irq);
		remove_one_irq_from_db(info->irq);
	}
	fclose(file);
	info->moved = 0; /*migration is done*/
}

void activate_mappings(void)
{
	for_each_irq(NULL, activate_mapping, NULL);
}
