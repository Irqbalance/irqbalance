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
 * This file tries to map numa affinity of pci devices to their interrupts
 * In addition the PCI class information is used to refine the classification
 * of interrupt sources 
 */
#include "config.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <dirent.h>

#include "irqbalance.h"

#define SYSFS_NODE_PATH "/sys/devices/system/node"

GList *numa_nodes = NULL;

static void add_one_node(int nodeid)
{
	char path[PATH_MAX];
	struct topo_obj *new;

	new = calloc(1, sizeof(struct topo_obj));
	if (!new) {
		need_rebuild = 1;
		return;
	}

	if (nodeid == NUMA_NO_NODE) {
		cpus_setall(new->mask);
	} else {
		cpus_clear(new->mask);
		sprintf(path, "%s/node%d/cpumap", SYSFS_NODE_PATH, nodeid);
		process_one_line(path, get_mask_from_bitmap, &new->mask);
	}

	new->obj_type = OBJ_TYPE_NODE;	
	new->number = nodeid;
	new->obj_type_list = &numa_nodes;
	numa_nodes = g_list_append(numa_nodes, new);
}

void build_numa_node_list(void)
{
	DIR *dir;
	struct dirent *entry;

	/* Add the unspecified node */
	add_one_node(NUMA_NO_NODE);

	if (!numa_avail)
		return;

	dir = opendir(SYSFS_NODE_PATH);
	if (!dir)
		return;

	do {
		entry = readdir(dir);
		if (!entry)
			break;
		if ((entry->d_type == DT_DIR) &&
		    (strncmp(entry->d_name, "node", 4) == 0) &&
		    isdigit(entry->d_name[4])) {
			add_one_node(strtoul(&entry->d_name[4], NULL, 10));
		}
	} while (entry);
	closedir(dir);
}

void free_numa_node_list(void)
{
	g_list_free_full(numa_nodes, free_cpu_topo);
	numa_nodes = NULL;
}

static gint compare_node(gconstpointer a, gconstpointer b)
{
	const struct topo_obj *ai = a;
	const struct topo_obj *bi = b;

	return (ai->number == bi->number) ? 0 : 1;
}

void connect_cpu_mem_topo(struct topo_obj *p, void *data __attribute__((unused)))
{
	GList *entry;
	struct topo_obj *node;
	int len;

	len = g_list_length(p->numa_nodes);

	if (len == 0) {
		return;
	} else if (len > 1) {
		for_each_object(p->children, connect_cpu_mem_topo, NULL);
		return;
	}

	entry = g_list_first(p->numa_nodes);
	node = entry->data;

	if (p->obj_type == OBJ_TYPE_PACKAGE && !p->parent)
		p->parent = node;

	entry = g_list_find(node->children, p);
	if (!entry)
		node->children = g_list_append(node->children, p);
}

void dump_numa_node_info(struct topo_obj *d, void *unused __attribute__((unused)))
{
	char buffer[4096];

	log(TO_CONSOLE, LOG_INFO, "NUMA NODE NUMBER: %d\n", d->number);
	cpumask_scnprintf(buffer, 4096, d->mask); 
	log(TO_CONSOLE, LOG_INFO, "LOCAL CPU MASK: %s\n", buffer);
	log(TO_CONSOLE, LOG_INFO, "\n");
}

struct topo_obj *get_numa_node(int nodeid)
{
	struct topo_obj find;
	GList *entry;

	find.number = numa_avail ? nodeid : NUMA_NO_NODE;

	entry = g_list_find_custom(numa_nodes, &find, compare_node);
	return entry ? entry->data : NULL;
}

