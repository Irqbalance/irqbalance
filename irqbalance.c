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
#ifdef HAVE_LIBCAP_NG
#include <cap-ng.h>
#endif
#include "irqbalance.h"

int one_shot_mode;
int debug_mode;

int need_cpu_rescan;

extern cpumask_t banned_cpus;

static int counter;


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

int main(int argc, char** argv)
{
	if (argc>1 && strstr(argv[1],"debug"))
		debug_mode=1;
	if (argc>1 && strstr(argv[1],"oneshot"))
		one_shot_mode=1;

	if (getenv("IRQBALANCE_BANNED_CPUS"))  {
		cpumask_parse_user(getenv("IRQBALANCE_BANNED_CPUS"), strlen(getenv("IRQBALANCE_BANNED_CPUS")), banned_cpus);
	}

	if (getenv("IRQBALANCE_ONESHOT")) 
		one_shot_mode=1;

	if (getenv("IRQBALANCE_DEBUG")) 
		debug_mode=1;

	parse_cpu_tree();


	/* On single core UP systems irqbalance obviously has no work to do */
	if (core_count<2) 
		exit(EXIT_SUCCESS);
	/* On dual core/hyperthreading shared cache systems just do a one shot setup */
	if (cache_domain_count==1)
		one_shot_mode = 1;

	if (!debug_mode)
		if (daemon(0,0))
			exit(EXIT_FAILURE);

#ifdef HAVE_LIBCAP_NG
	// Drop capabilities
	capng_clear(CAPNG_SELECT_BOTH);
	capng_lock();
	capng_apply(CAPNG_SELECT_BOTH);
#endif

	parse_proc_interrupts();
	sleep(SLEEP_INTERVAL/4);
	reset_counts();
	parse_proc_interrupts();
	pci_numa_scan();
	calculate_workload();
	sort_irq_list();
	if (debug_mode)
		dump_workloads();
	
	while (1) {
		sleep_approx(SLEEP_INTERVAL);
		if (debug_mode)
			printf("\n\n\n-----------------------------------------------------------------------------\n");


		check_power_mode();
		parse_proc_interrupts();

		/* cope with cpu hotplug -- detected during /proc/interrupts parsing */
		if (need_cpu_rescan) {
			need_cpu_rescan = 0;
			/* if there's a hotplug event we better turn off power mode for a bit until things settle */
			power_mode = 0;
			if (debug_mode)
				printf("Rescanning cpu topology \n");
			reset_counts();
			clear_work_stats();

			clear_cpu_tree();
			parse_cpu_tree();
		}

		/* deal with NAPI */
		account_for_nic_stats();
		calculate_workload();

		/* to cope with dynamic configurations we scan for new numa information
		 * once every 5 minutes
		 */
		if (counter % NUMA_REFRESH_INTERVAL == 16)
			pci_numa_scan();

		calculate_placement();
		activate_mapping();
	
		if (debug_mode)
			dump_tree();
		if (one_shot_mode)
			break;
		counter++;
	}
	return EXIT_SUCCESS;
}
