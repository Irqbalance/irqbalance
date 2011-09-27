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
 * This file tries to map numa affinity of pci devices to their interrupts
 * In addition the PCI class information is used to refine the classification
 * of interrupt sources 
 */
#include "config.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>

#include "irqbalance.h"

#define SYSFS_NODE_PATH "sys/devices/system/node"

GList *numa_nodes = NULL;

void pci_numa_scan(void)
{
	int irq = -1;
	cpumask_t mask;
	int node_num;
	do {
		int type;
		irq = get_next_irq(irq);
		if (irq == -1)
			break;

		mask = find_irq_cpumask_prop(irq, IRQ_LCPU_MASK);

		node_num = find_irq_integer_prop(irq, IRQ_NUMA);

		type = find_irq_integer_prop(irq, IRQ_CLASS);

		add_interrupt_numa(irq, mask, node_num, type);
		
	} while (irq != -1);
}

void add_one_node(const char *nodename)
{
	char *path = alloca(strlen(SYSFS_NODE_PATH) + strlen(nodename) + 1);
	struct numa_node *new;
	char *cpustr;
	FILE *f;

	if (!path)
		return;
	new = calloc(1, sizeof(struct numa_node));
	if (!new)
		return;
	sprintf(path, "%s/%s/cpumap", SYSFS_NODE_PATH, nodename);
	f = fopen(path, "r");
	if (ferror(f)) {
		cpus_clear(new->local_cpus);
	} else {
		fscanf(f, "%as", &cpustr);
		if (!cpustr) {
			cpus_clear(new->local_cpus);
		} else {
			cpumask_parse_user(cpustr, strlen(cpustr), new->local_cpus);
			free(cpustr);
		}
	}
	
	new->number = strtoul(&nodename[4], NULL, 10);
	numa_nodes = g_list_append(numa_nodes, new);
}

void build_numa_node_list(void)
{
	DIR *dir = opendir(SYSFS_NODE_PATH);
	struct dirent *entry;

	do {
		entry = readdir(dir);
		if (!entry)
			break;
		if ((entry->d_type == DT_DIR) && (strstr("node", entry->d_name))) {
			add_one_node(entry->d_name);
		}
	} while (entry);
}

static void free_numa_node(gpointer data)
{
	free(data);
}

void free_numa_node_list(void)
{
	g_list_free_full(numa_nodes, free_numa_node);
	numa_nodes = NULL;
}

