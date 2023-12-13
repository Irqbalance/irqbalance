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
	unsigned long long min_load;
	unsigned long long adjustment_load;
	int load_sources;
	unsigned long long int deviations;
	long double std_deviation;
	unsigned int num_over;
	unsigned int num_under;
	unsigned int num_powersave;
	struct topo_obj *powersave;
};

static void gather_load_stats(struct topo_obj *obj, void *data)
{
	struct load_balance_info *info = data;

	if (info->load_sources == 0 || obj->load < info->min_load)
		info->min_load = obj->load;
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
	struct load_balance_info *lb_info = data;
	unsigned long delta_load = 0;

	/* Don't rebalance irqs that don't want or support it */
	if (info->level == BALANCE_NONE)
		return;

	/* Don't move cpus that only have one irq, regardless of load */
	if (g_list_length(info->assigned_obj->interrupts) <= 1)
		return;

	/* IRQs with a load of 1 have most likely not had any interrupts and
	 * aren't worth migrating
	 */
	if (info->load <= 1)
		return;

	if (migrate_ratio > 0) {
		delta_load = (lb_info->adjustment_load - lb_info->min_load) / migrate_ratio;
	}

	/* If we can migrate an irq without swapping the imbalance do it. */
	if ((lb_info->min_load + info->load) < delta_load + (lb_info->adjustment_load - info->load)) {
		lb_info->adjustment_load -= info->load;
		lb_info->min_load += info->load;
		if (lb_info->min_load > lb_info->adjustment_load) {
			lb_info->min_load = lb_info->adjustment_load;
		}
	} else
		return;

	log(TO_CONSOLE, LOG_INFO, "Selecting irq %d for rebalancing\n", info->irq);

	migrate_irq(&info->assigned_obj->interrupts, &rebalance_irq_list, info);

	info->assigned_obj = NULL;
}

static void migrate_overloaded_irqs(struct topo_obj *obj, void *data)
{
	struct load_balance_info *info = data;

	if (obj->powersave_mode)
		info->num_powersave++;

	if ((obj->load + info->std_deviation) <= info->avg_load) {
		info->num_under++;
		if (power_thresh != ULONG_MAX && !info->powersave)
			if (!obj->powersave_mode)
				info->powersave = obj;
	} else if ((obj->load - info->std_deviation) >=info->avg_load) {
		info->num_over++;
	}

	if ((obj->load > info->min_load) &&
	    (g_list_length(obj->interrupts) > 1)) {
		/* order the list from greatest to least workload */
		sort_irq_list(&obj->interrupts);
		/*
		 * Each irq carries a weighted average amount of load
		 * we think it's responsible for. This object's load is larger
		 * than the object with the minimum load. Select irqs for
		 * migration if we could move them to the minimum object
		 * without reversing the imbalance or until we only have one
		 * left.
		 */
		info->adjustment_load = obj->load;
		for_each_irq(obj->interrupts, move_candidate_irqs, info);
	}
}

static void force_irq_migration(struct irq_info *info, void *data __attribute__((unused)))
{
	migrate_irq(&info->assigned_obj->interrupts, &rebalance_irq_list, info);
	info->assigned_obj = NULL;
}

static void clear_powersave_mode(struct topo_obj *obj, void *data __attribute__((unused)))
{
	obj->powersave_mode = 0;
}

static void find_overloaded_objs(GList *name, struct load_balance_info *info) {
	memset(info, 0, sizeof(struct load_balance_info));
	for_each_object(name, gather_load_stats, info);
	info->load_sources = (info->load_sources == 0) ? 1 : (info->load_sources);
	info->avg_load = info->total_load / info->load_sources;
	for_each_object(name, compute_deviations, info);
	/* Don't divide by zero if there is a single load source */
	if (info->load_sources == 1)
		info->std_deviation = 0;
	else {
		info->std_deviation = (long double)(info->deviations / (info->load_sources - 1));
		info->std_deviation = sqrt(info->std_deviation);
	}

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

static void dump_workload(struct irq_info *info, void *unused __attribute__((unused)))
{
	log(TO_CONSOLE, LOG_INFO, "Interrupt %i node_num %d (class %s) has workload %lu \n",
	    info->irq, irq_numa_node(info)->number, classes[info->class], (unsigned long)info->load);
}

void dump_workloads(void)
{
	for_each_irq(NULL, dump_workload, NULL);
}

