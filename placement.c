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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

#include "types.h"
#include "irqbalance.h"


int power_mode;

extern GList *interrupts, *packages, *cache_domains, *cpus;

static uint64_t package_cost_func(struct interrupt *irq, struct package *package)
{
	int bonus = 0;
	int maxcount;
	/* moving to a cold package/cache/etc gets you a 3000 penalty */
	if (!cpus_intersects(irq->old_mask, package->mask))
		bonus = CROSS_PACKAGE_PENALTY;

	/* do a little numa affinity */
	if (!cpus_intersects(irq->numa_mask, package->mask))
		bonus += NUMA_PENALTY;

	/* but if the irq has had 0 interrupts for a while move it about more easily */
	if (irq->workload==0)
		bonus = bonus / 10;

	/* in power save mode, you better be on package 0, with overflow to the next package if really needed */
	if (power_mode)
		bonus += POWER_MODE_PACKAGE_THRESHOLD * package->number;

	/* if we're out of whack in terms of per class counts.. just block (except in power mode) */
	maxcount = (class_counts[irq->class] + package_count -1 ) / package_count;
	if (package->class_count[irq->class]>=maxcount && !power_mode)
		bonus += 300000;

	return irq->workload + bonus;
}

static uint64_t cache_domain_cost_func(struct interrupt *irq, struct cache_domain *cache_domain)
{
	int bonus = 0;
	/* moving to a cold cache gets you a 1500 penalty */
	if (!cpus_intersects(irq->old_mask, cache_domain->mask))
		bonus = CROSS_PACKAGE_PENALTY/2;

	/* do a little numa affinity */
	if (!cpus_intersects(irq->numa_mask, cache_domain->mask))
		bonus += NUMA_PENALTY;

	/* but if the irq has had 0 interrupts for a while move it about more easily */
	if (irq->workload==0)
		bonus = bonus / 10;


	/* pay 6000 for each previous interrupt of the same class */
	bonus += CLASS_VIOLATION_PENTALTY * cache_domain->class_count[irq->class];

	return irq->workload + bonus;
}

static uint64_t cpu_cost_func(struct interrupt *irq, struct cpu_core *cpu)
{
	int bonus = 0;
	/* moving to a colder core gets you a 1000 penalty */
	if (!cpus_intersects(irq->old_mask, cpu->mask))
		bonus = CROSS_PACKAGE_PENALTY/3;

	/* do a little numa affinity */
	if (!cpus_intersects(irq->numa_mask, cpu->mask))
		bonus += NUMA_PENALTY;

	/* but if the irq has had 0 interrupts for a while move it about more easily */
	if (irq->workload==0)
		bonus = bonus / 10;

	/* 
	 * since some chipsets only place at the first cpu, give a tiny preference to non-first
	 * cpus for specifically placed interrupts 
	 */
	if (first_cpu(cpu->cache_mask)==cpu->number)
		bonus++;
        


	/* pay 6000 for each previous interrupt of the same class */
	bonus += CLASS_VIOLATION_PENTALTY * cpu->class_count[irq->class];

	return irq->workload + bonus;
}


static void place_cache_domain(struct package *package)
{
	GList *iter, *next;
	GList *pkg;
	struct interrupt *irq;
	struct cache_domain *cache_domain;


	iter = g_list_first(package->interrupts);
	while (iter) {
		struct cache_domain *best = NULL;
		uint64_t best_cost = INT_MAX;
		irq = iter->data;

		if (irq->balance_level <= BALANCE_PACKAGE) {
			iter = g_list_next(iter);
			continue;
		}
		pkg = g_list_first(package->cache_domains);
		while (pkg) {
			uint64_t newload;

			cache_domain = pkg->data;
			newload = cache_domain->workload + cache_domain_cost_func(irq, cache_domain);
			if (newload < best_cost)  {
				best = cache_domain;
				best_cost = newload;
			}

			pkg = g_list_next(pkg);
		}
		if (best) {
			next = g_list_next(iter);
			package->interrupts = g_list_delete_link(package->interrupts, iter);
			
			best->workload += irq->workload + 1;
			best->interrupts=g_list_append(best->interrupts, irq);
			best->class_count[irq->class]++;
			irq->mask = best->mask;
			iter = next;
		} else
			iter = g_list_next(iter);
	}
}


static void place_core(struct cache_domain *cache_domain)
{
	GList *iter, *next;
	GList *pkg;
	struct interrupt *irq;
	struct cpu_core *cpu;


	iter = g_list_first(cache_domain->interrupts);
	while (iter) {
		struct cpu_core *best = NULL;
		uint64_t best_cost = INT_MAX;
		irq = iter->data;

		/* if the irq isn't per-core policy and is not very busy, leave it at cache domain level */
		if (irq->balance_level <= BALANCE_CACHE && irq->workload < CORE_SPECIFIC_THRESHOLD && !one_shot_mode) {
			iter = g_list_next(iter);
			continue;
		}
		pkg = g_list_first(cache_domain->cpu_cores);
		while (pkg) {
			uint64_t newload;

			cpu = pkg->data;
			newload = cpu->workload + cpu_cost_func(irq, cpu);
			if (newload < best_cost)  {
				best = cpu;
				best_cost = newload;
			}

			pkg = g_list_next(pkg);
		}
		if (best) {
			next = g_list_next(iter);
			cache_domain->interrupts = g_list_delete_link(cache_domain->interrupts, iter);
			
			best->workload += irq->workload + 1;
			best->interrupts=g_list_append(best->interrupts, irq);
			best->class_count[irq->class]++;
			irq->mask = best->mask;
			iter = next;
		} else
			iter = g_list_next(iter);
	}
}


static void place_packages(GList *list)
{
	GList *iter;
	GList *pkg;
	struct interrupt *irq;
	struct package *package;


	iter = g_list_first(list);
	while (iter) {
		struct package *best = NULL;
		uint64_t best_cost = INT_MAX;
		irq = iter->data;
		if (irq->balance_level == BALANCE_NONE) {
			iter = g_list_next(iter);
			continue;
		}
		pkg = g_list_first(packages);
		while (pkg) {
			uint64_t newload;

			package = pkg->data;
			newload = package->workload + package_cost_func(irq, package);
			if (newload < best_cost)  {
				best = package;
				best_cost = newload;
			}

			pkg = g_list_next(pkg);
		}
		if (best) {
			best->workload += irq->workload + 1;
			best->interrupts=g_list_append(best->interrupts, irq);
			best->class_count[irq->class]++;
			irq->mask = best->mask;
		}
		iter = g_list_next(iter);
	}
}



static void do_unroutables(void)
{
	struct package *package;
	struct cache_domain *cache_domain;
	struct cpu_core *cpu;
	struct interrupt *irq;
	GList *iter, *inter;

	inter = g_list_first(interrupts);
	while (inter) {
		irq = inter->data;
		inter = g_list_next(inter);
		if (irq->balance_level != BALANCE_NONE)
			continue;

		iter = g_list_first(packages);
		while (iter) {
			package = iter->data;
			if (cpus_intersects(package->mask, irq->mask))
				package->workload += irq->workload;
			iter = g_list_next(iter);
		}

		iter = g_list_first(cache_domains);
		while (iter) {
			cache_domain = iter->data;
			if (cpus_intersects(cache_domain->mask, irq->mask))
				cache_domain->workload += irq->workload;
			iter = g_list_next(iter);
		}
		iter = g_list_first(cpus);
		while (iter) {
			cpu = iter->data;
			if (cpus_intersects(cpu->mask, irq->mask))
				cpu->workload += irq->workload;
			iter = g_list_next(iter);
		}
	}
}


void calculate_placement(void)
{
	struct package *package;
	struct cache_domain *cache_domain;
	GList *iter;
	/* first clear old data */ 
	clear_work_stats();
	sort_irq_list();
	do_unroutables();

	place_packages(interrupts);
	iter = g_list_first(packages);
	while (iter) {
		package = iter->data;
		place_cache_domain(package);
		iter = g_list_next(iter);
	}

	iter = g_list_first(cache_domains);
	while (iter) {
		cache_domain = iter->data;
		place_core(cache_domain);
		iter = g_list_next(iter);
	}
}
