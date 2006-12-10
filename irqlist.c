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
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>

#include "types.h"
#include "irqbalance.h"

GList *interrupts;



/*
 * This function classifies and reads various things from /proc about a specific irq 
 */
static void investigate(struct interrupt *irq, int number)
{
	DIR *dir;
	struct dirent *entry;
	char *c, *c2;
	int nr;
	char buf[PATH_MAX];
	sprintf(buf, "/proc/irq/%i", number);
	dir = opendir(buf);
	do {
		entry = readdir(dir);
		if (!entry)
			break;
		if (strcmp(entry->d_name,"smp_affinity")==0) {
			char *line = NULL;
			size_t size = 0;
			FILE *file;
			sprintf(buf, "/proc/irq/%i/smp_affinity", number);
			file = fopen(buf, "r");
			if (!file)
				continue;
			if (getline(&line, &size, file)==0) {
				free(line);
				fclose(file);
				continue;
			}
			cpumask_parse_user(line, strlen(line), irq->mask);
			fclose(file);
			free(line);
		} else {
			irq->class = find_class(irq, entry->d_name);
		}

	} while (entry);
	closedir(dir);	
	irq->balance_level = map_class_to_level[irq->class];

	/* next, check the IRQBALANCE_BANNED_INTERRUPTS env variable for blacklisted irqs */
	c = getenv("IRQBALANCE_BANNED_INTERRUPTS");
	if (!c)
		return;

	do {
		nr = strtoul(c, &c2, 10);
		if (c!=c2 && nr == number)
			irq->balance_level = BALANCE_NONE;
		c = c2;
	} while (c!=c2 && c2!=NULL);
}


/*
 * Set the number of interrupts received for a specific irq;
 * create the irq metadata if there is none yet
 */
void set_interrupt_count(int number, uint64_t count, cpumask_t *mask)
{
	GList *item;
	struct interrupt *irq;

	if (count < MIN_IRQ_COUNT && !one_shot_mode)
		return; /* no need to track or set interrupts sources without any activity since boot
		 	   but allow for a few (20) boot-time-only interrupts */

	item = g_list_first(interrupts);
	while (item) {
		irq = item->data;

		if (irq->number == number) {
			irq->count = count;
			return;
		}
		item = g_list_next(item);
	}
	/* new interrupt */
	irq = malloc(sizeof(struct interrupt));
	if (!irq)
		return;
	memset(irq, 0, sizeof(struct interrupt));
	irq->number = number;
	irq->count = count;
	investigate(irq, number);
	if (irq->balance_level == BALANCE_NONE)
		irq->mask = *mask;

	interrupts = g_list_append(interrupts, irq);
}

/* 
 * Add extra irqs to a specific irq metadata structure;
 * if no such metadata exists, do nothing at all
 */
void add_interrupt_count(int number, uint64_t count, int type)
{
	GList *item;
	struct interrupt *irq;

	if (!count)
		return;

	item = g_list_first(interrupts);
	while (item) {
		irq = item->data;
		item = g_list_next(item);

		if (irq->number == number) {
			irq->extra += count;
			if (irq->class < type && irq->balance_level != BALANCE_NONE)  {
				irq->class = type;
				irq->balance_level = map_class_to_level[irq->class];
			}
			return;
		}
	}
}

/*
 * Set the numa affinity mask for a specific interrupt if there
 * is metadata for the interrupt; do nothing if no such data
 * exists.
 */
void add_interrupt_numa(int number, cpumask_t mask, int type)
{
	GList *item;
	struct interrupt *irq;

	item = g_list_first(interrupts);
	while (item) {
		irq = item->data;
		item = g_list_next(item);

		if (irq->number == number) {
			cpus_or(irq->numa_mask, irq->numa_mask, mask);
			if (irq->class < type && irq->balance_level != BALANCE_NONE)  {
				irq->class = type;
				irq->balance_level = map_class_to_level[irq->class];
			}
			return;
		}
	}
}

void calculate_workload(void)
{
	int i;
	GList *item;
	struct interrupt *irq;

	for (i=0; i<7; i++)
		class_counts[i]=0;
	item = g_list_first(interrupts);
	while (item) {
		irq = item->data;
		item = g_list_next(item);

		irq->workload = irq->count - irq->old_count + irq->workload/3 + irq->extra;
		class_counts[irq->class]++;
		irq->old_count = irq->count;
		irq->extra = 0;
	}
}

void reset_counts(void)
{
	GList *item;
	struct interrupt *irq;
	item = g_list_first(interrupts);
	while (item) {
		irq = item->data;
		item = g_list_next(item);
		irq->old_count = irq->count;
		irq->extra = 0;

	}
}

void dump_workloads(void)
{
	GList *item;
	struct interrupt *irq;
	item = g_list_first(interrupts);
	while (item) {
		irq = item->data;
		item = g_list_next(item);

		printf("Interrupt %i (class %s) has workload %lu \n", irq->number, classes[irq->class], (unsigned long)irq->workload);

	}
}


static gint sort_irqs(gconstpointer A, gconstpointer B)
{
	struct interrupt *a, *b;
	a = (struct interrupt*)A;
	b = (struct interrupt*)B;

	if (a->class < b->class)
		return 1;
	if (a->class > b->class)
		return -1;
	if (a->workload < b->workload)
		return 1;
	if (a->workload > b->workload)
		return -1;
	if (a<b)
		return 1;
	return -1;
	
}

void sort_irq_list(void)
{
	/* sort by class first (high->low) and then by workload (high->low) */
	interrupts = g_list_sort(interrupts, sort_irqs);
}
