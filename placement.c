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
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

#include "types.h"
#include "irqbalance.h"


int power_mode;

GList *rebalance_irq_list;

static uint64_t package_cost_func(struct irq_info *irq, struct package *package)
{
	int bonus = 0;
	int maxcount;

	/* but if the irq has had 0 interrupts for a while move it about more easily */
	if (irq->workload==0)
		bonus = bonus / 10;

	/* in power save mode, you better be on package 0, with overflow to the next package if really needed */
	if (power_mode)
		bonus += POWER_MODE_PACKAGE_THRESHOLD * package->common.number;

	/* if we're out of whack in terms of per class counts.. just block (except in power mode) */
	maxcount = (class_counts[irq->class] + package_count -1 ) / package_count;
	if (package->class_count[irq->class]>=maxcount && !power_mode)
		bonus += 300000;

	return irq->workload + bonus;
}

static uint64_t cache_domain_cost_func(struct irq_info *irq, struct cache_domain *cache_domain)
{
	int bonus = 0;

	/* but if the irq has had 0 interrupts for a while move it about more easily */
	if (irq->workload==0)
		bonus = bonus / 10;


	/* pay 6000 for each previous interrupt of the same class */
	bonus += CLASS_VIOLATION_PENTALTY * cache_domain->class_count[irq->class];

	/* try to avoid having a lot of MSI interrupt (globally, no by devide id) on
	 * cache domain */
	if ((irq->type == IRQ_TYPE_MSI) || (irq->type == IRQ_TYPE_MSIX))
		bonus += MSI_CACHE_PENALTY * cache_domain->class_count[irq->class];


	return irq->workload + bonus;
}

static uint64_t cpu_cost_func(struct irq_info *irq, struct cpu_core *cpu)
{
	int bonus = 0;

	/* but if the irq has had 0 interrupts for a while move it about more easily */
	if (irq->workload==0)
		bonus = bonus / 10;

	/* 
	 * since some chipsets only place at the first cpu, give a tiny preference to non-first
	 * cpus for specifically placed interrupts 
	 */
	if (first_cpu(cpu_cache_domain(cpu)->common.mask)==cpu->common.number)
		bonus++;

	/* pay 6000 for each previous interrupt of the same class */
	bonus += CLASS_VIOLATION_PENTALTY * cpu->class_count[irq->class];

	return irq->workload + bonus;
}

struct cache_domain_placement {
	struct irq_info *info;
	struct cache_domain *best;
	uint64_t best_cost;
};

static void find_best_cd(struct cache_domain *c, void *data)
{
	struct cache_domain_placement *best = data;
	uint64_t newload;

	newload = c->common.workload + cache_domain_cost_func(best->info, c);
	if (newload < best->best_cost) {
		best->best = c;
		best->best_cost = newload;
	}
}	

static void place_irq_in_cache_domain(struct irq_info *info, void *data)
{
	struct package *p = data;
	struct cache_domain_placement place;

	if (!info->moved)
		return;

	if (info->level <= BALANCE_PACKAGE)
		return;

	place.best_cost = INT_MAX;
	place.best = NULL;
	place.info = info;

	for_each_cache_domain(p->cache_domains, find_best_cd, &place);

	if (place.best) {
		migrate_irq(&p->common.interrupts, &place.best->common.interrupts, info);
		info->assigned_obj = (struct common_obj_data *)place.best;
		place.best->class_count[info->class]++;
	}

}
	
static void place_cache_domain(struct package *package, void *data __attribute__((unused)))
{
	if (package->common.interrupts)
		for_each_irq(package->common.interrupts, place_irq_in_cache_domain, package);
}


struct core_placement {
	struct cpu_core *best;
	uint64_t best_cost;
	struct irq_info *info;
};

static void place_irq_in_core(struct cpu_core *c, void *data)
{
	struct core_placement *best = data;
	uint64_t newload;

	newload = c->common.workload + cpu_cost_func(best->info, c);
	if (newload < best->best_cost) {
		best->best = c;
		best->best_cost = newload;
	}
}

static void place_core(struct irq_info *info, void *data)
{
	struct cache_domain *c = data;
	struct core_placement place;

	if (!info->moved)
		return;

	if ((info->level <= BALANCE_CACHE) &&
	    (!one_shot_mode))
		return;

	place.info = info;
	place.best = NULL;
	place.best_cost = INT_MAX;

	for_each_cpu_core(c->cpu_cores, place_irq_in_core, &place);

	if (place.best) {
		migrate_irq(&c->common.interrupts, &place.best->common.interrupts, info);
		info->assigned_obj = (struct common_obj_data *)place.best;
		place.best->common.workload += info->workload + 1;
	}

}

static void place_cores(struct cache_domain *cache_domain, void *data __attribute__((unused)))
{
	if (cache_domain->common.interrupts)
		for_each_irq(cache_domain->common.interrupts, place_core, cache_domain);
}

struct package_placement {
	struct irq_info *info;
	struct package *best;
	uint64_t best_cost;
};

static void find_best_package(struct package *p, void *data)
{
	uint64_t newload;
	struct package_placement *place = data;

	newload = p->common.workload + package_cost_func(place->info, p);
	if (newload < place->best_cost) {
		place->best = p;
		place->best_cost = newload;
	}
}

static void place_irq_in_package(struct irq_info *info, void *data)
{
	struct package_placement place;
	struct numa_node *n = data;

	if (!info->moved)
		return;

	if (info->level == BALANCE_NONE)
		return;

	place.best_cost = INT_MAX;
	place.best = NULL;
	place.info = info;

	for_each_package(n->packages, find_best_package, &place);

	if (place.best) {
		migrate_irq(&n->common.interrupts, &place.best->common.interrupts, info);
		info->assigned_obj = (struct common_obj_data *)place.best;
		place.best->common.workload += info->workload + 1;
		place.best->class_count[info->class]++;
	}
}

static void place_packages(struct numa_node *n, void *data __attribute__((unused)))
{
	if (n->common.interrupts)
		for_each_irq(n->common.interrupts, place_irq_in_package, n);
}

struct node_placement {
	struct irq_info *info;
	struct numa_node *best;
	uint64_t best_cost;
};

static void find_best_node(struct numa_node *n, void *data)
{
	struct node_placement *place = data;

	/*
 	 * Just find the least loaded node
 	 */
	if (n->common.workload < place->best_cost) {
		place->best = n;
		place->best_cost = n->common.workload;
	}
}

static void place_irq_in_node(struct irq_info *info, void *data __attribute__((unused)))
{
	struct node_placement place;

	if( info->level == BALANCE_NONE)
		return;

	if (irq_numa_node(info)->common.number != -1) {
		/*
 		 * This irq belongs to a device with a preferred numa node
 		 * put it on that node
 		 */
		migrate_irq(&rebalance_irq_list, &irq_numa_node(info)->common.interrupts, info);
		info->assigned_obj = (struct common_obj_data *)irq_numa_node(info);
		irq_numa_node(info)->common.workload += info->workload + 1;
		return;
	}

	place.best_cost = INT_MAX;
	place.best = NULL;
	place.info = info;

	for_each_numa_node(NULL, find_best_node, &place);

	if (place.best) {
		migrate_irq(&rebalance_irq_list, &place.best->common.interrupts, info);
		info->assigned_obj = (struct common_obj_data *)place.best;
		place.best->common.workload += info->workload + 1;
	}
}

static void validate_irq(struct irq_info *info, void *data)
{
	printf("Validating irq %d %p against %p\n", info->irq, info->assigned_obj, data);
	if (info->assigned_obj != data)
		printf("irq %d is wrong, points to %p, should be %p\n",
			info->irq, info->assigned_obj, data);
}

static void validate_package(struct package *p, void *data __attribute__((unused)))
{
	if (p->common.interrupts)
		for_each_irq(p->common.interrupts, validate_irq, p);
}

static void validate_cd(struct cache_domain *c, void *data __attribute__((unused)))
{
	if (c->common.interrupts)
		for_each_irq(c->common.interrupts, validate_irq, c);
}

static void validate_cpu(struct cpu_core *c, void *data __attribute__((unused)))
{
	if (c->common.interrupts)
		for_each_irq(c->common.interrupts, validate_irq, c);
}

static void validate_object_tree_placement()
{
	for_each_package(NULL, validate_package, NULL);	
	for_each_cache_domain(NULL, validate_cd, NULL);
	for_each_cpu_core(NULL, validate_cpu, NULL);
}

void calculate_placement(void)
{
	/* first clear old data */ 
	clear_work_stats();

	sort_irq_list(&rebalance_irq_list);

	for_each_irq(rebalance_irq_list, place_irq_in_node, NULL);
	for_each_numa_node(NULL, place_packages, NULL);
	for_each_package(NULL, place_cache_domain, NULL);
	for_each_cache_domain(NULL, place_cores, NULL);

	if (debug_mode)
		validate_object_tree_placement();
}
