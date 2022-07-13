/* 
 * Copyright (C) 2006, Intel Corporation
 * Copyright (C) 2012, Neil Horman <nhoramn@tuxdriver.com> 
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
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

#include "types.h"
#include "irqbalance.h"


GList *rebalance_irq_list;

struct obj_placement {
		struct topo_obj *best;
		uint64_t best_cost;
		struct irq_info *info;
};

static void find_best_object(struct topo_obj *d, void *data)
{
	struct obj_placement *best = (struct obj_placement *)data;
	uint64_t newload;

	/*
 	 * Don't consider the unspecified numa node here
 	 */
	if (numa_avail && (d->obj_type == OBJ_TYPE_NODE) && (d->number == NUMA_NO_NODE))
		return;

	/*
	 * also don't consider any node that doesn't have at least one cpu in
	 * the unbanned list
	 */
	if ((d->obj_type == OBJ_TYPE_NODE) &&
	    (!cpus_intersects(d->mask, unbanned_cpus)))
		return;

	if (d->powersave_mode)
		return;

	newload = d->load;
	if (newload < best->best_cost) {
		best->best = d;
		best->best_cost = newload;
	} else if (newload == best->best_cost) {
		if (g_list_length(d->interrupts) < g_list_length(best->best->interrupts)) {
			best->best = d;
		}
	}
}

static void find_best_object_for_irq(struct irq_info *info, void *data)
{
	struct obj_placement place;
	struct topo_obj *d = data;
	struct topo_obj *asign;

	if (!info->moved)
		return;

	switch (d->obj_type) {
	case OBJ_TYPE_NODE:
		if (info->level == BALANCE_NONE)
			return;
		break;

	case OBJ_TYPE_PACKAGE:
		if (info->level == BALANCE_PACKAGE)
			return;
		break;

	case OBJ_TYPE_CACHE:
		if (info->level == BALANCE_CACHE)
			return;
		break;

	case OBJ_TYPE_CPU:
		if (info->level == BALANCE_CORE)
			return;
		break;
	}

	place.info = info;
	place.best = NULL;
	place.best_cost = ULLONG_MAX;

	for_each_object(d->children, find_best_object, &place);

	asign = place.best;

	if (asign) {
		migrate_irq(&d->interrupts, &asign->interrupts, info);
		info->assigned_obj = asign;
		asign->load += info->load;
	}
}

static void place_irq_in_object(struct topo_obj *d, void *data __attribute__((unused)))
{
	if (g_list_length(d->interrupts) > 0)
		for_each_irq(d->interrupts, find_best_object_for_irq, d);
}

static void place_irq_in_node(struct irq_info *info, void *data __attribute__((unused)))
{
	struct obj_placement place;
	struct topo_obj *asign;

	if ((info->level == BALANCE_NONE) && cpus_empty(banned_cpus))
		return;

	if (irq_numa_node(info)->number != NUMA_NO_NODE || !numa_avail) {
		/*
		 * Need to make sure this node is elligible for migration
		 * given the banned cpu list
		 */
		if (!cpus_intersects(irq_numa_node(info)->mask, unbanned_cpus)) {
			log(TO_CONSOLE, LOG_WARNING, "There is no suitable CPU in node:%d.\n", irq_numa_node(info)->number);
			log(TO_CONSOLE, LOG_WARNING, "Irqbalance dispatch irq:%d to other node.\n", info->irq);
			goto find_placement;
		}

		/*
		 * This irq belongs to a device with a preferred numa node
		 * put it on that node
		 */
		migrate_irq(&rebalance_irq_list, &irq_numa_node(info)->interrupts, info);
		info->assigned_obj = irq_numa_node(info);
		irq_numa_node(info)->load += info->load + 1;

		return;
	}

find_placement:
	place.best_cost = ULLONG_MAX;
	place.best = NULL;
	place.info = info;

	for_each_object(numa_nodes, find_best_object, &place);

	asign = place.best;

	if (asign) {
		migrate_irq(&rebalance_irq_list, &asign->interrupts, info);
		info->assigned_obj = asign;
		asign->load += info->load;
	}
}

static void validate_irq(struct irq_info *info, void *data)
{
	if (info->assigned_obj != data)
		log(TO_CONSOLE, LOG_INFO, "object validation error: irq %d is wrong, points to %p, should be %p\n",
			info->irq, info->assigned_obj, data);
}

static void validate_object(struct topo_obj *d, void *data __attribute__((unused)))
{
	if (g_list_length(d->interrupts) > 0)
		for_each_irq(d->interrupts, validate_irq, d);
}

static void validate_object_tree_placement(void)
{
	for_each_object(packages, validate_object, NULL);	
	for_each_object(cache_domains, validate_object, NULL);
	for_each_object(cpus, validate_object, NULL);
}

void calculate_placement(void)
{
	sort_irq_list(&rebalance_irq_list);
	if (g_list_length(rebalance_irq_list) > 0) {
		for_each_irq(rebalance_irq_list, place_irq_in_node, NULL);
		for_each_object(numa_nodes, place_irq_in_object, NULL);
		for_each_object(packages, place_irq_in_object, NULL);
		for_each_object(cache_domains, place_irq_in_object, NULL);
	}
	if (debug_mode)
		validate_object_tree_placement();
}
