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
 * This file has the basic functions to manipulate interrupt metadata
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>

#include "types.h"
#include "irqbalance.h"



void get_affinity_hint(struct irq_info *irq, int number)
{
	char buf[PATH_MAX];
	cpumask_t tempmask;
	char *line = NULL;
	size_t size = 0;
	FILE *file;
	sprintf(buf, "/proc/irq/%i/affinity_hint", number);
	file = fopen(buf, "r");
	if (!file)
		return;
	if (getline(&line, &size, file)==0) {
		free(line);
		fclose(file);
		return;
	}
	cpumask_parse_user(line, strlen(line), tempmask);
	if (!__cpus_full(&tempmask, num_possible_cpus()))
		irq->affinity_hint = tempmask;
	fclose(file);
	free(line);
}

void build_workload(struct irq_info *info, void *unused __attribute__((unused)))
{
	info->workload = info->irq_count - info->last_irq_count + info->workload/3;
	class_counts[info->class]++;
	info->last_irq_count = info->irq_count;
}

void calculate_workload(void)
{
	int i;

	for (i=0; i<7; i++)
		class_counts[i]=0;
	for_each_irq(NULL, build_workload, NULL);
}

static void reset_irq_count(struct irq_info *info, void *unused __attribute__((unused)))
{
	info->last_irq_count = info->irq_count;
	info->irq_count = 0;
}

void reset_counts(void)
{
	for_each_irq(NULL, reset_irq_count, NULL);
}


static void dump_workload(struct irq_info *info, void *unused __attribute__((unused)))
{
	printf("Interrupt %i node_num %d (class %s) has workload %lu \n", info->irq, irq_numa_node(info)->number, classes[info->class], (unsigned long)info->workload);
}

void dump_workloads(void)
{
	for_each_irq(NULL, dump_workload, NULL);
}

