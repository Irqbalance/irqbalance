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

#define SYSFS_NODE_PATH "/sys/devices/system/node"

GList *numa_nodes = NULL;

struct numa_node unspecified_node = {
	.common = {
		.workload = 0,
		.number = -1,
		.mask = CPU_MASK_ALL,
		.interrupts = NULL,
	},	
	.packages = NULL,
};

static void add_one_node(const char *nodename)
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
		cpus_clear(new->common.mask);
	} else {
		fscanf(f, "%as", &cpustr);
		if (!cpustr) {
			cpus_clear(new->common.mask);
		} else {
			cpumask_parse_user(cpustr, strlen(cpustr), new->common.mask);
			free(cpustr);
		}
	}
	
	new->common.number = strtoul(&nodename[4], NULL, 10);
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
		if ((entry->d_type == DT_DIR) && (strstr(entry->d_name, "node"))) {
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

static gint compare_node(gconstpointer a, gconstpointer b)
{
	const struct numa_node *ai = a;
	const struct numa_node *bi = b;

	return (ai->common.number == bi->common.number) ? 0 : 1;
}

void add_package_to_node(struct package *p, int nodeid)
{
	struct numa_node find, *node;
	find.common.number = nodeid;
	GList *entry;

	find.common.number = nodeid;
	entry = g_list_find_custom(numa_nodes, &find, compare_node);

	if (!entry) {
		if (debug_mode)
			printf("Could not find numa node for node id %d\n", nodeid);
		return;
	}

	node = entry->data;

	node->packages = g_list_append(node->packages, p);
	p->numa_node = node;
}

void dump_numa_node_info(struct numa_node *node, void *unused __attribute__((unused)))
{
	char buffer[4096];

	printf("NUMA NODE NUMBER: %d\n", node->common.number);
	cpumask_scnprintf(buffer, 4096, node->common.mask); 
	printf("LOCAL CPU MASK: %s\n", buffer);
	printf("\n");
}

void for_each_numa_node(GList *list, void(*cb)(struct numa_node *node, void *data), void *data)
{
	GList *entry, *next;

	entry = g_list_first(list ? list : numa_nodes);

	while (entry) {
		next = g_list_next(entry);
		cb(entry->data, data);
		entry = next;
	}
}

struct numa_node *get_numa_node(int nodeid)
{
	struct numa_node find;
	GList *entry;

	if (nodeid == -1)
		return &unspecified_node;

	find.common.number = nodeid;

	entry = g_list_find_custom(numa_nodes, &find, compare_node);
	return entry ? entry->data : NULL;
}

