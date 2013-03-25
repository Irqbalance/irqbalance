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
 * This file has the basic functions to manipulate interrupt metadata
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>

#include "types.h"
#include "irqbalance.h"



struct load_balance_info {
	unsigned long long int total_load;
	unsigned long long avg_load;
	int load_sources;
	unsigned long long int deviations;
	long double std_deviation;
	unsigned int num_within;
	unsigned int num_over;
	unsigned int num_under;
	unsigned int num_powersave;
	struct topo_obj *powersave;
};

static void gather_load_stats(struct topo_obj *obj, void *data)
{
	struct load_balance_info *info = data;

	info->total_load += obj->load;
	info->load_sources += 1;
}

static void compute_deviations(struct topo_obj *obj, void *data)
{
	struct load_balance_info *info = data;
	unsigned long long int deviation;

	deviation = (obj->load > info->avg_load) ?
		obj->load - info->avg_load :
		info->avg_load - obj->load;

	info->deviations += (deviation * deviation);
}

static void move_candidate_irqs(struct irq_info *info, void *data)
{
	int *remaining_deviation = (int *)data;

	/* never move an irq that has an afinity hint when 
 	 * hint_policy is HINT_POLICY_EXACT 
 	 */
	if (hint_policy == HINT_POLICY_EXACT)
		if (!cpus_empty(info->affinity_hint))
			return;

	/* Don't rebalance irqs that don't want it */
	if (info->level == BALANCE_NONE)
		return;

	/* Don't move cpus that only have one irq, regardless of load */
	if (g_list_length(info->assigned_obj->interrupts) <= 1)
		return;

	/* Stop rebalancing if we've estimated a full reduction of deviation */
	if (*remaining_deviation <= 0)
		return;

	*remaining_deviation -= info->load;

	log(TO_CONSOLE, LOG_INFO, "Selecting irq %d for rebalancing\n", info->irq);

	migrate_irq(&info->assigned_obj->interrupts, &rebalance_irq_list, info);

	info->assigned_obj = NULL;
}

static void migrate_overloaded_irqs(struct topo_obj *obj, void *data)
{
	struct load_balance_info *info = data;
	int deviation;

	if (obj->powersave_mode)
		info->num_powersave++;

	/*
 	 * Don't rebalance irqs on objects whos load is below the average
 	 */
	if (obj->load <= info->avg_load) {
		if ((obj->load + info->std_deviation) <= info->avg_load) {
			info->num_under++;
			if (power_thresh != ULONG_MAX && !info->powersave)
				if (!obj->powersave_mode)
					info->powersave = obj;
		} else
			info->num_within++; 
		return;
	}

	deviation = obj->load - info->avg_load;

	if ((deviation > info->std_deviation) &&
	    (g_list_length(obj->interrupts) > 1)) {

		info->num_over++;
		/*
 		 * We have a cpu that is overloaded and 
 		 * has irqs that can be moved to fix that
 		 */

		/* order the list from least to greatest workload */
		sort_irq_list(&obj->interrupts);
		/*
 		 * Each irq carries a weighted average amount of load
 		 * we think its responsible for.  Set deviation to be the load
 		 * of the difference between this objects load and the averate,
 		 * and migrate irqs until we only have one left, or until that
 		 * difference reaches zero
 		 */
		for_each_irq(obj->interrupts, move_candidate_irqs, &deviation);
	} else
		info->num_within++;

}

static void force_irq_migration(struct irq_info *info, void *data __attribute__((unused)))
{
	migrate_irq(&info->assigned_obj->interrupts, &rebalance_irq_list, info);
}

static void clear_powersave_mode(struct topo_obj *obj, void *data __attribute__((unused)))
{
	obj->powersave_mode = 0;
}

static void find_overloaded_objs(GList *name, struct load_balance_info *info) {
	int ___load_sources;
	memset(info, 0, sizeof(struct load_balance_info));
	for_each_object(name, gather_load_stats, info);
	info->load_sources = (info->load_sources == 0) ? 1 : (info->load_sources);
	info->avg_load = info->total_load / info->load_sources;
	for_each_object(name, compute_deviations, info);
	___load_sources = (info->load_sources == 1) ? 1 : (info->load_sources - 1);
	info->std_deviation = (long double)(info->deviations / info->load_sources - 1);
	info->std_deviation = sqrt(info->std_deviation);

	/*
 	 * For two core systems, std deviation will always at best equal the 
 	 * deviation of any data point, so cut it in half.  that should allow us
 	 * to trigger migration on the more loaded of the two cpus.  Note that
 	 * we actually divide by 4 here rather than two.  We do that so as to
 	 * keep the bessel correction in tact.
 	 */
	if (info->load_sources <=2)
		info->std_deviation = info->std_deviation/4;

	for_each_object(name, migrate_overloaded_irqs, info);
}

void update_migration_status(void)
{
	struct load_balance_info info;
	find_overloaded_objs(cpus, &info);
	if (power_thresh != ULONG_MAX && cycle_count > 5) {
		if (!info.num_over && (info.num_under >= power_thresh) && info.powersave) {
			log(TO_ALL, LOG_INFO, "cpu %d entering powersave mode\n", info.powersave->number);
			info.powersave->powersave_mode = 1;
			if (g_list_length(info.powersave->interrupts) > 0)
				for_each_irq(info.powersave->interrupts, force_irq_migration, NULL);
		} else if ((info.num_over) && (info.num_powersave)) {
			log(TO_ALL, LOG_INFO, "Load average increasing, re-enabling all cpus for irq balancing\n");
			for_each_object(cpus, clear_powersave_mode, NULL);
		}
	}
	find_overloaded_objs(cache_domains, &info);
	find_overloaded_objs(packages, &info);
	find_overloaded_objs(numa_nodes, &info);
}


static void reset_irq_count(struct irq_info *info, void *unused __attribute__((unused)))
{
	info->last_irq_count = info->irq_count;
	info->irq_count = 0;
}

void reset_counts(void)
{
	for_each_irq(NULL, reset_irq_count, NULL);
}


static void dump_workload(struct irq_info *info, void *unused __attribute__((unused)))
{
	log(TO_CONSOLE, LOG_INFO, "Interrupt %i node_num %d (class %s) has workload %lu \n",
	    info->irq, irq_numa_node(info)->number, classes[info->class], (unsigned long)info->load);
}

void dump_workloads(void)
{
	for_each_irq(NULL, dump_workload, NULL);
}

