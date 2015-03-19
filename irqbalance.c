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
#include "config.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <sys/time.h>
#include <syslog.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef HAVE_GETOPT_LONG 
#include <getopt.h>
#endif

#ifdef HAVE_LIBCAP_NG
#include <cap-ng.h>
#endif
#include "irqbalance.h"

volatile int keep_going = 1;
int one_shot_mode;
int debug_mode;
int foreground_mode;
int numa_avail;
int journal_logging = 0;
int need_rescan;
unsigned int log_mask = TO_ALL;
char * log_indent;
enum hp_e global_hint_policy = HINT_POLICY_IGNORE;
unsigned long power_thresh = ULONG_MAX;
unsigned long deepest_cache = 2;
unsigned long long cycle_count = 0;
char *pidfile = NULL;
char *banscript = NULL;
char *polscript = NULL;
long HZ;

static void sleep_approx(int seconds)
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
	{"foreground", 0, NULL, 'f'},
	{"hintpolicy", 1, NULL, 'h'},
	{"powerthresh", 1, NULL, 'p'},
	{"banirq", 1 , NULL, 'i'},
	{"banscript", 1, NULL, 'b'},
	{"deepestcache", 1, NULL, 'c'},
	{"policyscript", 1, NULL, 'l'},
	{"pid", 1, NULL, 's'},
	{"journal", 0, NULL, 'j'},
	{0, 0, 0, 0}
};

static void usage(void)
{
	log(TO_CONSOLE, LOG_INFO, "irqbalance [--oneshot | -o] [--debug | -d] [--foreground | -f] [--journal | -j] [--hintpolicy= | -h [exact|subset|ignore]]\n");
	log(TO_CONSOLE, LOG_INFO, "	[--powerthresh= | -p <off> | <n>] [--banirq= | -i <n>] [--policyscript= | -l <script>] [--pid= | -s <file>] [--deepestcache= | -c <n>]\n");
}

static void parse_command_line(int argc, char **argv)
{
	int opt;
	int longind;
	unsigned long val;

	while ((opt = getopt_long(argc, argv,
		"odfjh:i:p:s:c:b:l:",
		lopts, &longind)) != -1) {

		switch(opt) {
			case '?':
				usage();
				exit(1);
				break;
			case 'b':
#ifndef INCLUDE_BANSCRIPT
				/*
				 * Banscript is no longer supported unless
				 * explicitly enabled
				 */
				log(TO_CONSOLE, LOG_INFO, "--banscript is not supported on this version of irqbalance, please use --policyscript\n");
				usage();
				exit(1);
#endif
				banscript = strdup(optarg);
				break;
			case 'c':
				deepest_cache = strtoul(optarg, NULL, 10);
				if (deepest_cache == ULONG_MAX || deepest_cache < 1) {
					usage();
					exit(1);
				}
				break;
			case 'd':
				debug_mode=1;
				foreground_mode=1;
				break;
			case 'f':
				foreground_mode=1;
				break;
			case 'h':
				if (!strncmp(optarg, "exact", strlen(optarg)))
					global_hint_policy = HINT_POLICY_EXACT;
				else if (!strncmp(optarg, "subset", strlen(optarg)))
					global_hint_policy = HINT_POLICY_SUBSET;
				else if (!strncmp(optarg, "ignore", strlen(optarg)))
					global_hint_policy = HINT_POLICY_IGNORE;
				else {
					usage();
					exit(1);
				}
				break;
			case 'i':
				val = strtoull(optarg, NULL, 10);
				if (val == ULONG_MAX) {
					usage();
					exit(1);
				}
				add_cl_banned_irq((int)val);
				break;
			case 'l':
				polscript = strdup(optarg);
				break;
			case 'p':
				if (!strncmp(optarg, "off", strlen(optarg)))
					power_thresh = ULONG_MAX;
				else {
					power_thresh = strtoull(optarg, NULL, 10);
					if (power_thresh == ULONG_MAX) {
						usage();
						exit(1);
					}
				}
				break;
			case 'o':
				one_shot_mode=1;
				break;
			case 's':
				pidfile = optarg;
				break;
			case 'j':
				journal_logging=1;
				foreground_mode=1;
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
static void build_object_tree(void)
{
	build_numa_node_list();
	parse_cpu_tree();
	rebuild_irq_db();
}

static void free_object_tree(void)
{
	free_numa_node_list();
	clear_cpu_tree();
	free_irq_db();
}

static void dump_object_tree(void)
{
	for_each_object(numa_nodes, dump_numa_node_info, NULL);
}

static void force_rebalance_irq(struct irq_info *info, void *data __attribute__((unused)))
{
	if (info->level == BALANCE_NONE)
		return;

	if (info->assigned_obj == NULL)
		rebalance_irq_list = g_list_append(rebalance_irq_list, info);
	else
		migrate_irq(&info->assigned_obj->interrupts, &rebalance_irq_list, info);

	info->assigned_obj = NULL;
}

static void handler(int signum)
{
	(void)signum;
	keep_going = 0;
}

static void force_rescan(int signum)
{
	(void)signum;
	if (cycle_count)
		need_rescan = 1;
}

int main(int argc, char** argv)
{
	struct sigaction action, hupaction;
	sigset_t sigset, old_sigset;

	sigemptyset(&sigset);
	sigaddset(&sigset,SIGINT);
	sigaddset(&sigset,SIGHUP);
	sigaddset(&sigset,SIGTERM);
	sigaddset(&sigset,SIGUSR1);
	sigaddset(&sigset,SIGUSR2);
	sigprocmask(SIG_BLOCK, &sigset, &old_sigset);
#ifdef HAVE_GETOPT_LONG
	parse_command_line(argc, argv);
#else /* ! HAVE_GETOPT_LONG */
	if (argc>1 && strstr(argv[1],"--debug")) {
		debug_mode=1;
		foreground_mode=1;
	}
	if (argc>1 && strstr(argv[1],"--foreground"))
		foreground_mode=1;
	if (argc>1 && strstr(argv[1],"--oneshot"))
		one_shot_mode=1;
	if (argc>1 && strstr(argv[1],"--journal")) {
		journal_logging=1;
		foreground_mode=1;
	}
#endif /* HAVE_GETOPT_LONG */

	/*
 	 * Open the syslog connection
 	 */
	openlog(argv[0], 0, LOG_DAEMON);

	if (getenv("IRQBALANCE_ONESHOT")) 
		one_shot_mode=1;

	if (getenv("IRQBALANCE_DEBUG")) 
		debug_mode=1;

	/*
 	 * If we are't in debug mode, don't dump anything to the console
 	 * note that everything goes to the console before we check this
 	 */
	if (journal_logging)
		log_indent = strdup("....");
	else
		log_indent = strdup("    ");

	if (!debug_mode)
		log_mask &= ~TO_CONSOLE;

	if (numa_available() > -1) {
		numa_avail = 1;
	} else 
		log(TO_CONSOLE, LOG_INFO, "This machine seems not NUMA capable.\n");

	if (geteuid() != 0)
		log(TO_ALL, LOG_WARNING, "Irqbalance hasn't been executed under root privileges, thus it won't in fact balance interrupts.\n");

	if (banscript) {
		char *note = "Please note that --banscript is deprecated, please use --policyscript instead";
		log(TO_ALL, LOG_WARNING, "%s\n", note);
	}

	HZ = sysconf(_SC_CLK_TCK);
	if (HZ == -1) {
		log(TO_ALL, LOG_WARNING, "Unable to determin HZ defaulting to 100\n");
		HZ = 100;
	}

	build_object_tree();
	if (debug_mode)
		dump_object_tree();


	/* On single core UP systems irqbalance obviously has no work to do */
	if (core_count<2) {
		char *msg = "Balancing is ineffective on systems with a "
			    "single cpu.  Shutting down\n";

		log(TO_ALL, LOG_WARNING, "%s", msg);
		exit(EXIT_SUCCESS);
	}

	if (!foreground_mode) {
		int pidfd = -1;
		if (daemon(0,0))
			exit(EXIT_FAILURE);
		/* Write pidfile */
		if (pidfile && (pidfd = open(pidfile,
			O_WRONLY | O_CREAT | O_EXCL | O_TRUNC,
			S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) >= 0) {
			char str[16];
			snprintf(str, sizeof(str), "%u\n", getpid());
			write(pidfd, str, strlen(str));
			close(pidfd);
		}
	}

	action.sa_handler = handler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	sigaction(SIGINT, &action, NULL);
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGUSR1, &action, NULL);
	sigaction(SIGUSR2, &action, NULL);

	hupaction.sa_handler = force_rescan;
	sigemptyset(&hupaction.sa_mask);
	hupaction.sa_flags = 0;
	sigaction(SIGHUP, &hupaction, NULL);

	sigprocmask(SIG_SETMASK, &old_sigset, NULL);

#ifdef HAVE_LIBCAP_NG
	// Drop capabilities
	capng_clear(CAPNG_SELECT_BOTH);
	capng_lock();
	capng_apply(CAPNG_SELECT_BOTH);
#endif

	for_each_irq(NULL, force_rebalance_irq, NULL);

	parse_proc_interrupts();
	parse_proc_stat();

	while (keep_going) {
		sleep_approx(SLEEP_INTERVAL);
		log(TO_CONSOLE, LOG_INFO, "\n\n\n-----------------------------------------------------------------------------\n");


		clear_work_stats();
		parse_proc_interrupts();
		parse_proc_stat();

		/* cope with cpu hotplug -- detected during /proc/interrupts parsing */
		if (need_rescan) {
			need_rescan = 0;
			cycle_count = 0;
			log(TO_CONSOLE, LOG_INFO, "Rescanning cpu topology \n");
			clear_work_stats();

			free_object_tree();
			build_object_tree();
			for_each_irq(NULL, force_rebalance_irq, NULL);
			parse_proc_interrupts();
			parse_proc_stat();
			sleep_approx(SLEEP_INTERVAL);
			clear_work_stats();
			parse_proc_interrupts();
			parse_proc_stat();
		} 

		if (cycle_count)	
			update_migration_status();

		calculate_placement();
		activate_mappings();
	
		if (debug_mode)
			dump_tree();
		if (one_shot_mode)
			keep_going = 0;
		cycle_count++;

	}
	free_object_tree();

	/* Remove pidfile */
	if (!foreground_mode && pidfile)
		unlink(pidfile);

	return EXIT_SUCCESS;
}
