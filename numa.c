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

struct common_obj_data unspecified_node = {
	.load = 0,
	.number = -1,
	.mask = CPU_MASK_ALL,
	.interrupts = NULL,
	.children = NULL,
	.parent = NULL,
};

static void add_one_node(const char *nodename)
{
	char *path = alloca(strlen(SYSFS_NODE_PATH) + strlen(nodename) + 1);
	struct common_obj_data *new;
	char *cpustr;
	FILE *f;

	if (!path)
		return;
	new = calloc(1, sizeof(struct common_obj_data));
	if (!new)
		return;
	sprintf(path, "%s/%s/cpumap", SYSFS_NODE_PATH, nodename);
	f = fopen(path, "r");
	if (ferror(f)) {
		cpus_clear(new->mask);
	} else {
		fscanf(f, "%as", &cpustr);
		if (!cpustr) {
			cpus_clear(new->mask);
		} else {
			cpumask_parse_user(cpustr, strlen(cpustr), new->mask);
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
	const struct common_obj_data *ai = a;
	const struct common_obj_data *bi = b;

	return (ai->number == bi->number) ? 0 : 1;
}

void add_package_to_node(struct common_obj_data *p, int nodeid)
{
	struct common_obj_data find, *node;
	find.number = nodeid;
	GList *entry;

	find.number = nodeid;
	entry = g_list_find_custom(numa_nodes, &find, compare_node);

	if (!entry) {
		if (debug_mode)
			printf("Could not find numa node for node id %d\n", nodeid);
		return;
	}

	node = entry->data;

	node->children = g_list_append(node->children, p);
	p->parent = node;
}

void dump_numa_node_info(struct common_obj_data *d, void *unused __attribute__((unused)))
{
	char buffer[4096];

	printf("NUMA NODE NUMBER: %d\n", d->number);
	cpumask_scnprintf(buffer, 4096, d->mask); 
	printf("LOCAL CPU MASK: %s\n", buffer);
	printf("\n");
}

void for_each_numa_node(GList *list, void(*cb)(struct common_obj_data *node, void *data), void *data)
{
	GList *entry, *next;

	entry = g_list_first(list ? list : numa_nodes);

	while (entry) {
		next = g_list_next(entry);
		cb(entry->data, data);
		entry = next;
	}
}

struct common_obj_data *get_numa_node(int nodeid)
{
	struct common_obj_data find;
	GList *entry;

	if (nodeid == -1)
		return &unspecified_node;

	find.number = nodeid;

	entry = g_list_find_custom(numa_nodes, &find, compare_node);
	return entry ? entry->data : NULL;
}

