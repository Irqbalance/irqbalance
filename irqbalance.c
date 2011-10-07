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
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <sys/time.h>
#include <syslog.h>
#include <unistd.h>
#ifdef HAVE_GETOPT_LONG 
#include <getopt.h>
#endif

#ifdef HAVE_LIBCAP_NG
#include <cap-ng.h>
#endif
#include "irqbalance.h"

int one_shot_mode;
int debug_mode;
int numa_avail;
int need_cpu_rescan;
extern cpumask_t banned_cpus;
static int counter;
enum hp_e hint_policy = HINT_POLICY_SUBSET;


void sleep_approx(int seconds)
{
	struct timespec ts;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	ts.tv_sec = seconds;
	ts.tv_nsec = -tv.tv_usec*1000;
	while (ts.tv_nsec < 0) {
		ts.tv_sec--;
		ts.tv_nsec += 1000000000;
	}
	nanosleep(&ts, NULL);
}

#ifdef HAVE_GETOPT_LONG
struct option lopts[] = {
	{"oneshot", 0, NULL, 'o'},
	{"debug", 0, NULL, 'd'},
	{"hintpolicy", 1, NULL, 'h'},
	{0, 0, 0, 0}
};

static void usage(void)
{
	printf("irqbalance [--oneshot | -o] [--debug | -d] [--hintpolicy= | -h [exact|subset|ignore]]");
}

static void parse_command_line(int argc, char **argv)
{
	int opt;
	int longind;

	while ((opt = getopt_long(argc, argv,
		"odh:",
		lopts, &longind)) != -1) {

		switch(opt) {
			case '?':
				usage();
				exit(1);
			case 'd':
				debug_mode=1;
				break;
			case 'h':
				if (!strncmp(optarg, "exact", strlen(optarg)))
					hint_policy = HINT_POLICY_EXACT;
				else if (!strncmp(optarg, "subset", strlen(optarg)))
					hint_policy = HINT_POLICY_SUBSET;
				else if (!strncmp(optarg, "ignore", strlen(optarg)))
					hint_policy = HINT_POLICY_IGNORE;
				else {
					usage();
					exit(1);
				}
				break;
			case 'o':
				one_shot_mode=1;
				break;
		}
	}
}
#endif

/*
 * This builds our object tree.  The Heirarchy is pretty straightforward
 * At the top are numa_nodes
 * All CPU packages belong to a single numa_node
 * All Cache domains belong to a CPU package
 * All CPU cores belong to a cache domain
 *
 * Objects are built in that order (top down)
 *
 * Object workload is the aggregate sum of the
 * workload of the objects below it
 */
static void build_object_tree()
{
	build_numa_node_list();
	parse_cpu_tree();
	rebuild_irq_db();
}

static void free_object_tree()
{
	free_numa_node_list();
	clear_cpu_tree();
	free_irq_db();
}

static void dump_object_tree()
{
	for_each_numa_node(NULL, dump_numa_node_info, NULL);
}

static void force_rebalance_irq(struct irq_info *info, void *data __attribute__((unused)))
{
	if (info->level == BALANCE_NONE)
		return;

	migrate_irq((info->assigned_obj ? &info->assigned_obj->interrupts : NULL),
		     &rebalance_irq_list, info);
	info->assigned_obj = NULL;
}

int main(int argc, char** argv)
{
	int compute_migration_status=0;

#ifdef HAVE_GETOPT_LONG
	parse_command_line(argc, argv);
#else
	if (argc>1 && strstr(argv[1],"--debug"))
		debug_mode=1;
	if (argc>1 && strstr(argv[1],"--oneshot"))
		one_shot_mode=1;
#endif
	if (getenv("IRQBALANCE_BANNED_CPUS"))  {
		cpumask_parse_user(getenv("IRQBALANCE_BANNED_CPUS"), strlen(getenv("IRQBALANCE_BANNED_CPUS")), banned_cpus);
	}

	if (getenv("IRQBALANCE_ONESHOT")) 
		one_shot_mode=1;

	if (getenv("IRQBALANCE_DEBUG")) 
		debug_mode=1;

	if (numa_available() > -1) {
		numa_avail = 1;
	} else {
		if (debug_mode)
			printf("This machine seems not NUMA capable.\n");
	}


	build_object_tree();
	if (debug_mode)
		dump_object_tree();


	/* On single core UP systems irqbalance obviously has no work to do */
	if (core_count<2) 
		exit(EXIT_SUCCESS);
	/* On dual core/hyperthreading shared cache systems just do a one shot setup */
	if (cache_domain_count==1)
		one_shot_mode = 1;

	if (!debug_mode)
		if (daemon(0,0))
			exit(EXIT_FAILURE);

	openlog(argv[0], 0, LOG_DAEMON);

#ifdef HAVE_LIBCAP_NG
	// Drop capabilities
	capng_clear(CAPNG_SELECT_BOTH);
	capng_lock();
	capng_apply(CAPNG_SELECT_BOTH);
#endif

	for_each_irq(NULL, force_rebalance_irq, NULL);

	while (1) {
		sleep_approx(SLEEP_INTERVAL);
		if (debug_mode)
			printf("\n\n\n-----------------------------------------------------------------------------\n");


		check_power_mode();
		parse_proc_interrupts();
		parse_proc_stat();

		/* cope with cpu hotplug -- detected during /proc/interrupts parsing */
		if (need_cpu_rescan) {
			need_cpu_rescan = 0;
			/* if there's a hotplug event we better turn off power mode for a bit until things settle */
			power_mode = 0;
			if (debug_mode)
				printf("Rescanning cpu topology \n");
			reset_counts();
			clear_work_stats();

			free_object_tree();
			build_object_tree();
			for_each_irq(NULL, force_rebalance_irq, NULL);
			compute_migration_status=0;
		} 

		if (compute_migration_status)	
			update_migration_status();
		else
			compute_migration_status=1;


		calculate_placement();
		activate_mappings();
	
		if (debug_mode)
			dump_tree();
		if (one_shot_mode)
			break;
		clear_work_stats();
		counter++;

	}
	free_object_tree();
	return EXIT_SUCCESS;
}
