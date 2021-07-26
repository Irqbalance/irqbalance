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
		if (!cpus_intersects(irq_numa_node(info)->mask, unbanned_cpus))
			goto find_placement;
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


extern GList *interrupts_db;


gint compare_numbers(gconstpointer a, gconstpointer b)
{
	const struct topo_obj *ai = a;
	const struct topo_obj *bi = b;

	return ai->number - bi->number;
}


void clear_move_state(struct irq_info *irq, void *data __attribute__((unused))) {
	irq->moved = 0;
}

void spread_irq(struct irq_info *irq, void *data) {
	char buf[BUFSIZ];


	uint64_t *cpu_list = (uint64_t *)data;
	uint64_t cpu_count = g_list_length(cpus);
	struct topo_obj find;
	GList *entry = NULL;

	uint64_t i = 1, k = 1;
	uint64_t min_idx = 0, min_count = UINT64_MAX;

	cpumask_scnprintf(buf, BUFSIZ, irq->cpumask);

	if (irq->moved)
		return ;


	while (i <= cpu_count) {
		if (!cpu_isset(i, irq->cpumask) && min_count > cpu_list[i-1]) {
			min_count = cpu_list[i-1];
			min_idx = i;
		}
		

		i++;
	}

	find.number = min_idx;
	entry = g_list_find_custom(cpus, &find, compare_numbers);

	// didn't find the best, we choose the cpu bound with least irqs
	if (entry) {
		struct topo_obj *target_cpu = entry->data;
                cpu_list[min_idx - 1]++;
		migrate_irq(&irq->assigned_obj->interrupts, &target_cpu->interrupts, irq);
		irq->assigned_obj = target_cpu;

		if (debug_mode)
			printf("irq %d assigned to cpu %d\n", irq->irq, target_cpu->number);

	}
	else {
		
		for (k = 1; k <= cpu_count; k++) {
			if (cpu_isset(k, unbanned_cpus) && cpu_list[k-1] < min_count) {
				min_idx = k;
				min_count = cpu_list[k-1];
			}
		}
		find.number = min_idx-1;
		entry = g_list_find_custom(cpus, &find, compare_numbers);
		if (entry) {
			cpu_list[min_idx-1]++;
			struct topo_obj *target_cpu = entry->data;
			migrate_irq(&irq->assigned_obj->interrupts, &target_cpu->interrupts, irq);
			irq->assigned_obj = target_cpu;

			if (debug_mode)
				printf("irq %d assigned to cpu %d\n", irq->irq, target_cpu->number);
		}
	}
	irq->moved = 1;

	if (debug_mode)
		printf("irq %d ---- mask : %s --- assigned to cpu-%d\n", irq->irq, buf, irq->assigned_obj->number);
}


void spread_irq_of_obj(struct topo_obj *obj, void *data) {
	if (g_list_length(obj->interrupts) > 0)
		for_each_irq(obj->interrupts, spread_irq, data);
}


void spread_irqs(void) {
	uint64_t cpu_count = g_list_length(cpus);
	uint64_t irqs_count = g_list_length(interrupts_db);
	uint64_t banned_irqs_count = g_list_length(cl_banned_irqs);

	uint64_t *cpu_list = (uint64_t *)malloc(sizeof(uint64_t) * cpu_count);
	memset(cpu_list, 0, sizeof(uint64_t) * cpu_count);

	if (debug_mode) {
		printf("%lu cpu(s) in total\n", cpu_count);
		printf("%lu irq(s) in total\n", irqs_count);
		printf("%lu irq(s) banned in total\n", banned_irqs_count);
	}

	for_each_irq(interrupts_db, clear_move_state, NULL);

	for_each_object(numa_nodes, spread_irq_of_obj, (void *)cpu_list);
	for_each_object(packages, spread_irq_of_obj, (void *)cpu_list);
	for_each_object(cache_domains, spread_irq_of_obj, (void *)cpu_list);
	for_each_object(cpus, spread_irq_of_obj, (void *)cpu_list);

	free(cpu_list);
}
