/* 
 * Copyright (C) 2006, Intel Corporation
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


static void activate_mapping(struct irq_info *info, void *data __attribute__((unused)))
{
	char buf[PATH_MAX];
	FILE *file;

	if (info->level == BALANCE_NONE)
		return;
	if (cpus_equal(info->mask, info->old_mask))
		return;

	sprintf(buf, "/proc/irq/%i/smp_affinity", info->irq);
	file = fopen(buf, "w");
	if (!file)
		return;

	cpumask_scnprintf(buf, PATH_MAX, info->mask);
	fprintf(file, "%s", buf);
	fclose(file);
	info->old_mask = info->mask;
}

void activate_mappings(void)
{
	for_each_irq(NULL, activate_mapping, NULL);
}
