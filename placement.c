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

/*
 * Threshold for slot-based penalty. CPUs with slots_left >= this value
 * receive zero penalty (considered to have ample headroom).
 * CPUs with fewer slots receive an increasing penalty to prefer
 * CPUs with more capacity.
 */
#define SLOTS_PENALTY_THRESHOLD 10

/*
 * Penalty multiplier per slot below threshold.
 *
 * The value 1000 is chosen because:
 * - Typical IRQ load values range from thousands to millions
 * - A penalty of 1000-9000 (for slots 9 down to 1) is significant
 *   enough to influence placement when loads are similar
 * - But not so large as to override load-based decisions entirely
 *   for lightly loaded CPUs
 *
 * Example: CPU with load=5000 and slots=2 has adjusted_cost = 5000 + 8000 = 13000
 *          CPU with load=8000 and slots=10 has adjusted_cost = 8000 + 0 = 8000
 *          → Prefers the higher-load CPU with more headroom
 *
 * CPUs with slots_left >= SLOTS_PENALTY_THRESHOLD get zero penalty,
 * so their placement is determined purely by load balancing.
 */
#define SLOTS_PENALTY_FACTOR 1000

static void find_best_object(struct topo_obj *d, void *data)
{
	struct obj_placement *best = (struct obj_placement *)data;
	uint64_t newload;
	uint64_t adjusted_cost;
	uint64_t best_adjusted_cost;
	uint64_t slots_penalty;
	uint64_t best_slots_penalty;

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

	if (d->slots_left <= 0)
		return;

	newload = d->load;

	/*
	 * Factor in slots_left to prefer CPUs with more available capacity.
	 * When loads are similar, prefer CPUs with more headroom to reduce
	 * likelihood of ENOSPC. Using a penalty system: lower slots = higher
	 * effective cost.
	 *
	 * Penalty calculation: slots < SLOTS_PENALTY_THRESHOLD adds penalty
	 * Cast to uint64_t to ensure safe arithmetic with newload.
	 */
	if (d->slots_left < SLOTS_PENALTY_THRESHOLD)
		slots_penalty = (uint64_t)(SLOTS_PENALTY_THRESHOLD - d->slots_left) * SLOTS_PENALTY_FACTOR;
	else
		slots_penalty = 0;
	adjusted_cost = newload + slots_penalty;

	if (best->best) {
		if (best->best->slots_left < SLOTS_PENALTY_THRESHOLD)
			best_slots_penalty = (uint64_t)(SLOTS_PENALTY_THRESHOLD - best->best->slots_left) * SLOTS_PENALTY_FACTOR;
		else
			best_slots_penalty = 0;
		best_adjusted_cost = best->best_cost + best_slots_penalty;
	} else {
		best_adjusted_cost = ULLONG_MAX;
	}

	if (adjusted_cost < best_adjusted_cost) {
		best->best = d;
		best->best_cost = newload;
	} else if (adjusted_cost == best_adjusted_cost) {
		/*
		 * Tie-breaker: first prefer the CPU with more slots_left
		 * (more headroom). During normal operation slots_left is
		 * INT_MAX for all CPUs (see clear_slots()), so fall back to
		 * the original interrupt-count comparison to keep IRQs
		 * spread across CPUs that currently hold fewer interrupts.
		 */
		if (!best->best ||
		    d->slots_left > best->best->slots_left ||
		    (d->slots_left == best->best->slots_left &&
		     g_list_length(d->interrupts) < g_list_length(best->best->interrupts))) {
			best->best = d;
			best->best_cost = newload;
		}
	}
}

static void find_best_object_for_irq(struct irq_info *info, void *data)
{
	struct obj_placement place;
	struct topo_obj *d = data;

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

	if (place.best)
		migrate_irq_obj(d, place.best, info);
}

static void place_irq_in_object(struct topo_obj *d, void *data __attribute__((unused)))
{
	if (g_list_length(d->interrupts) > 0)
		for_each_irq(d->interrupts, find_best_object_for_irq, d);
}

static void place_irq_in_node(struct irq_info *info, void *data __attribute__((unused)))
{
	struct obj_placement place;

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
		migrate_irq_obj(NULL, irq_numa_node(info), info);

		return;
	}

find_placement:
	place.best_cost = ULLONG_MAX;
	place.best = NULL;
	place.info = info;

	for_each_object(numa_nodes, find_best_object, &place);

	if (place.best)
		migrate_irq_obj(NULL, place.best, info);
}

static void validate_irq(struct irq_info *info, void *data)
{
	if (info->assigned_obj != data)
		log(TO_CONSOLE, LOG_INFO, "object validation error: irq %d is wrong, points to %p, should be %p\n",
			info->irq, (void*)info->assigned_obj, data);
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
