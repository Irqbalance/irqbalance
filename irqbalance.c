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
#include <inttypes.h>
#ifdef HAVE_GETOPT_LONG 
#include <getopt.h>
#endif
#ifdef HAVE_LIBCAP_NG
#include <cap-ng.h>
#endif
#ifdef HAVE_IRQBALANCEUI
#include <sys/un.h>
#include <sys/socket.h>
#endif
#include "irqbalance.h"
#include "thermal.h"

volatile int keep_going = 1;
int one_shot_mode;
int debug_mode;
int foreground_mode;
int numa_avail;
int journal_logging = 0;
int need_rescan;
int need_rebuild;
unsigned int log_mask = TO_ALL;
const char *log_indent;
unsigned long power_thresh = ULONG_MAX;
unsigned long deepest_cache = 2;
unsigned long long cycle_count = 0;
char *pidfile = NULL;
char *polscript = NULL;
long HZ;
int sleep_interval = SLEEP_INTERVAL;
int last_interval;
GMainLoop *main_loop;

char *cpu_ban_string = NULL;
unsigned long migrate_ratio = 0;

#ifdef HAVE_IRQBALANCEUI
int socket_fd;
char socket_name[64];
char *banned_cpumask_from_ui = NULL;
#endif

#ifdef HAVE_GETOPT_LONG
struct option lopts[] = {
	{"oneshot", 0, NULL, 'o'},
	{"debug", 0, NULL, 'd'},
	{"foreground", 0, NULL, 'f'},
	{"powerthresh", 1, NULL, 'p'},
	{"banirq", 1 , NULL, 'i'},
	{"deepestcache", 1, NULL, 'c'},
	{"policyscript", 1, NULL, 'l'},
	{"pid", 1, NULL, 's'},
	{"journal", 0, NULL, 'j'},
	{"banmod", 1 , NULL, 'm'},
	{"interval", 1 , NULL, 't'},
	{"version", 0, NULL, 'V'},
	{"migrateval", 1, NULL, 'e'},
	{0, 0, 0, 0}
};

static void usage(void)
{
	log(TO_CONSOLE, LOG_INFO, "irqbalance [--oneshot | -o] [--debug | -d] [--foreground | -f] [--journal | -j]\n");
	log(TO_CONSOLE, LOG_INFO, "	[--powerthresh= | -p <off> | <n>] [--banirq= | -i <n>] [--banmod= | -m <module>] [--policyscript= | -l <script>]\n");
	log(TO_CONSOLE, LOG_INFO, "	[--pid= | -s <file>] [--deepestcache= | -c <n>] [--interval= | -t <n>] [--migrateval= | -e <n>]\n");
}

static void version(void)
{
	log(TO_CONSOLE, LOG_INFO, "irqbalance version " VERSION "\n");
}

static void parse_command_line(int argc, char **argv)
{
	int opt;
	int longind;
	unsigned long val;
	char *endptr;

	while ((opt = getopt_long(argc, argv,
		"odfjVi:p:s:c:l:m:t:e:",
		lopts, &longind)) != -1) {

		switch(opt) {
			case '?':
				usage();
				exit(1);
				break;
			case 'V':
				version();
				exit(1);
				break;
			case 'c':
				deepest_cache = strtoul(optarg, &endptr, 10);
				if (optarg == endptr || deepest_cache == ULONG_MAX || deepest_cache < 1) {
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
			case 'i':
				val = strtoull(optarg, &endptr, 10);
				if (optarg == endptr || val == ULONG_MAX) {
					usage();
					exit(1);
				}
				add_cl_banned_irq((int)val);
				break;
			case 'l':
				free(polscript);
				polscript = strdup(optarg);
				break;
			case 'm':
				add_cl_banned_module(optarg);
				break;
			case 'p':
				if (!strncmp(optarg, "off", strlen(optarg)))
					power_thresh = ULONG_MAX;
				else {
					power_thresh = strtoull(optarg, &endptr, 10);
					if (optarg == endptr || power_thresh == ULONG_MAX) {
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
			case 't':
				sleep_interval = strtol(optarg, &endptr, 10);
				if (optarg == endptr || sleep_interval < 1) {
					usage();
					exit(1);
				}
				break;
			case 'e':
				migrate_ratio = strtoul(optarg, &endptr, 10);
				if (optarg == endptr) {
					usage();
					exit(1);
				}
				break;
		}
	}
}
#else /* ! HAVE_GETOPT_LONG */
static void parse_command_line(int argc, char **argv)
{
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
}
#endif /* HAVE_GETOPT_LONG */

/*
 * This builds our object tree.  The Hierarchy is typically pretty
 * straightforward.
 * At the top are numa_nodes
 * CPU packages belong to a single numa_node, unless the cache domains are in
 * separate nodes.  In that case, the cache domain's parent is the package, but
 * the numa nodes point to the cache domains instead of the package as their
 * children.  This allows us to maintain the CPU hierarchy while adjusting for
 * alternate memory topologies that are present on recent processor.
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

void force_rebalance_irq(struct irq_info *info, void *data __attribute__((unused)))
{
	if (info->level == BALANCE_NONE)
		return;

	if (info->assigned_obj == NULL)
		rebalance_irq_list = g_list_append(rebalance_irq_list, info);
	else
		migrate_irq(&info->assigned_obj->interrupts, &rebalance_irq_list, info);

	info->assigned_obj = NULL;
}

gboolean handler(gpointer data __attribute__((unused)))
{
	keep_going = 0;
	g_main_loop_quit(main_loop);
	return TRUE;
}

gboolean force_rescan(gpointer data __attribute__((unused)))
{
	if (cycle_count)
		need_rescan = 1;
	return TRUE;
}

gboolean scan(gpointer data __attribute__((unused)))
{
	log(TO_CONSOLE, LOG_INFO, "\n\n\n-----------------------------------------------------------------------------\n");
	clear_work_stats();
	parse_proc_interrupts();


	/* cope with cpu hotplug -- detected during /proc/interrupts parsing */
	while (keep_going && (need_rescan || need_rebuild)) {
		int try_times = 0;

		need_rescan = 0;
		cycle_count = 0;
		log(TO_CONSOLE, LOG_INFO, "Rescanning cpu topology \n");
		clear_work_stats();

		do {
			free_object_tree();
			if (++try_times > 3) {
				log(TO_CONSOLE, LOG_WARNING, "Rescanning cpu topology: fail\n");
				goto out;
			}

			need_rebuild = 0;
			build_object_tree();
		} while (need_rebuild);

		for_each_irq(NULL, force_rebalance_irq, NULL);
		parse_proc_interrupts();
		parse_proc_stat();
		return TRUE;
	}

	parse_proc_stat();

	if (cycle_count)	
		update_migration_status();

	calculate_placement();
	activate_mappings();

out:
	if (debug_mode)
		dump_tree();
	if (one_shot_mode)
		keep_going = 0;
	cycle_count++;

	/* sleep_interval may be changed by socket */
	if (last_interval != sleep_interval) {
		last_interval = sleep_interval;
		g_timeout_add_seconds(sleep_interval, scan, NULL);
		return FALSE;
	}

	if (keep_going) {
		return TRUE;
	} else {
		g_main_loop_quit(main_loop);
		return FALSE;
	}
}

void get_irq_data(struct irq_info *irq, void *data)
{
	char **irqdata = (char **)data;
	char *newptr = NULL;

	if (!*irqdata)
		newptr = calloc(24 + 1 + 11 + 20 + 20 + 11, 1);
	else
		newptr = realloc(*irqdata, strlen(*irqdata) + 24 + 1 + 11 + 20 + 20 + 11);

	if (!newptr)
		return;

	*irqdata = newptr;

	sprintf(*irqdata + strlen(*irqdata),
			"IRQ %d LOAD %" PRIu64 " DIFF %" PRIu64 " CLASS %d ", irq->irq, irq->load,
			(irq->irq_count - irq->last_irq_count), irq->class);
}

void get_object_stat(struct topo_obj *object, void *data)
{
	char **stats = (char **)data;	
	char *irq_data = NULL;
	char *newptr = NULL;
	size_t irqdlen;

	if (g_list_length(object->interrupts) > 0) {
		for_each_irq(object->interrupts, get_irq_data, &irq_data);
	}

	irqdlen = irq_data ? strlen(irq_data) : 0;
	/*
	 * Note, the size in both conditional branches below is made up as follows:
	 * strlen(irq_data) - self explanitory
	 * 31 - The size of "TYPE  NUMBER  LOAD  SAVE_MODE  "
	 * 11 - The maximal size of a %d printout
	 * 20 - The maximal size of a %lu printout
	 * 1 - The trailing string terminator
	 * This should be adjusted if the string in the sprintf is changed
	 */
	if (!*stats) {
		newptr = calloc(irqdlen + 31 + 11 + 20 + 11 + 1, 1);
	} else {
		newptr = realloc(*stats, strlen(*stats) + irqdlen + 31 + 11 + 20 + 11 + 1);
	}

	if (!newptr) {
		free(irq_data);
		return;
	}

	*stats = newptr;

	sprintf(*stats + strlen(*stats), "TYPE %d NUMBER %d LOAD %" PRIu64 " SAVE_MODE %d %s",
			object->obj_type, object->number, object->load,
			object->powersave_mode, irq_data ? irq_data : "");
	free(irq_data);
	if (object->obj_type != OBJ_TYPE_CPU) {
		for_each_object(object->children, get_object_stat, data);
	}
}

#ifdef HAVE_IRQBALANCEUI
gboolean sock_handle(gint fd, GIOCondition condition, gpointer user_data __attribute__((unused)))
{
	char buff[500];
	int sock;
	int recv_size = 0;
	int valid_user = 0;

	struct iovec iov = { buff, 500 };
	struct msghdr msg = { 0 };
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = malloc(CMSG_SPACE(sizeof(struct ucred)));
	msg.msg_controllen = CMSG_SPACE(sizeof(struct ucred));

	struct cmsghdr *cmsg;

	if (condition == G_IO_IN) {
		sock = accept(fd, NULL, NULL);
		if (sock < 0) {
			log(TO_ALL, LOG_WARNING, "Connection couldn't be accepted.\n");
			goto out;
		}
		if ((recv_size = recvmsg(sock, &msg, 0)) < 0) {
			log(TO_ALL, LOG_WARNING, "Error while receiving data.\n");
			goto out_close;
		}
		cmsg = CMSG_FIRSTHDR(&msg);
		if (!cmsg) {
			log(TO_ALL, LOG_WARNING, "Connection no memory.\n");
			goto out_close;
		}
		if ((cmsg->cmsg_level == SOL_SOCKET) &&
				(cmsg->cmsg_type == SCM_CREDENTIALS)) {
			struct ucred *credentials = (struct ucred *) CMSG_DATA(cmsg);
			if (!credentials->uid) {
				valid_user = 1;
			}
		}
		if (!valid_user) {
			log(TO_ALL, LOG_INFO, "Permission denied for user to connect to socket.\n");
			goto out_close;
		}

		if (!strncmp(buff, "stats", strlen("stats"))) {
			char *stats = NULL;
			for_each_object(numa_nodes, get_object_stat, &stats);
			send(sock, stats, strlen(stats), 0);
			free(stats);
		}
		if (!strncmp(buff, "settings ", strlen("settings "))) {
			if (!(strncmp(buff + strlen("settings "), "sleep ",
							strlen("sleep ")))) {
				char *sleep_string = malloc(
						sizeof(char) * (recv_size - strlen("settings sleep ") + 1));

				if (!sleep_string)
					goto out_close;
				strncpy(sleep_string, buff + strlen("settings sleep "),
						recv_size - strlen("settings sleep "));
				sleep_string[recv_size - strlen("settings sleep ")] = '\0';
				int new_iterval = strtoul(sleep_string, NULL, 10);
				if (new_iterval >= 1) {
					sleep_interval = new_iterval;
				}
				free(sleep_string);
			} else if (!(strncmp(buff + strlen("settings "), "ban irqs ",
							strlen("ban irqs ")))) {
				char *end;
				char *irq_string = malloc(
						sizeof(char) * (recv_size - strlen("settings ban irqs ") + 1));

				if (!irq_string)
					goto out_close;
				strncpy(irq_string, buff + strlen("settings ban irqs "),
						recv_size - strlen("settings ban irqs "));
				irq_string[recv_size - strlen("settings ban irqs ")] = '\0';
				g_list_free_full(cl_banned_irqs, free);
				cl_banned_irqs = NULL;
				need_rescan = 1;
				if (!strncmp(irq_string, "NONE", strlen("NONE"))) {
					free(irq_string);
					goto out_close;
				}
				int irq = strtoul(irq_string, &end, 10);
				do {
					add_cl_banned_irq(irq);
				} while((irq = strtoul(end, &end, 10)));
				free(irq_string);
			} else if (!(strncmp(buff + strlen("settings "), "cpus ",
							strlen("cpus")))) {
				banned_cpumask_from_ui = NULL;
				free(cpu_ban_string);
				cpu_ban_string = NULL;

				cpu_ban_string = malloc(
						sizeof(char) * (recv_size - strlen("settings cpus ") + 1));

				if (!cpu_ban_string)
					goto out_close;
				strncpy(cpu_ban_string, buff + strlen("settings cpus "),
						recv_size - strlen("settings cpus "));
				cpu_ban_string[recv_size - strlen("settings cpus ")] = '\0';
				banned_cpumask_from_ui = strtok(cpu_ban_string, " ");
				if (!strncmp(banned_cpumask_from_ui, "NULL", strlen("NULL"))) {
					banned_cpumask_from_ui = NULL;
					free(cpu_ban_string);
					cpu_ban_string = NULL;
				}
				need_rescan = 1;
			}
		}
		if (!strncmp(buff, "setup", strlen("setup"))) {
			char banned[512];
			char *setup = calloc(strlen("SLEEP  ") + 11 + 1, 1);
			char *newptr = NULL;

			if (!setup)
				goto out_close;
			snprintf(setup, strlen("SLEEP  ") + 11 + 1, "SLEEP %d ", sleep_interval);
			if(g_list_length(cl_banned_irqs) > 0) {
				for_each_irq(cl_banned_irqs, get_irq_data, &setup);
			}
			cpumask_scnprintf(banned, 512, banned_cpus);
			newptr = realloc(setup, strlen(setup) + strlen(banned) + 7 + 1);
			if (!newptr)
				goto out_free_setup;

			setup = newptr;
			snprintf(setup + strlen(setup), strlen(banned) + 7 + 1,
					"BANNED %s", banned);
			send(sock, setup, strlen(setup), 0);
out_free_setup:
			free(setup);
		}

out_close:
		close(sock);
	}

out:
	free(msg.msg_control);
	return TRUE;
}

int init_socket()
{
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(struct sockaddr_un));

	socket_fd = socket(AF_LOCAL, SOCK_STREAM, 0);
	if (socket_fd < 0) {
		log(TO_ALL, LOG_WARNING, "Socket couldn't be created.\n");
		return 1;
	}

	/*
	 * First try to create a file-based socket in tmpfs.  If that doesn't
	 * succeed, fall back to an abstract socket (non file-based).
	 */
	addr.sun_family = AF_UNIX;
	snprintf(socket_name, 64, "%s/%s%d.sock", SOCKET_TMPFS, SOCKET_PATH, getpid());
	strncpy(addr.sun_path, socket_name, sizeof(addr.sun_path));
	if (bind(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		log(TO_ALL, LOG_WARNING, "Daemon couldn't be bound to the file-based socket.\n");

		/* Try binding to abstract */
		memset(&addr, 0, sizeof(struct sockaddr_un));
		addr.sun_family = AF_UNIX;
		if (bind(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			log(TO_ALL, LOG_WARNING, "Daemon couldn't be bound to the abstract socket, bailing out.\n");
			return 1;
		}
	}

	int optval = 1;
	if (setsockopt(socket_fd, SOL_SOCKET, SO_PASSCRED, &optval, sizeof(optval)) < 0) {
		log(TO_ALL, LOG_WARNING, "Unable to set socket options.\n");
		return 1;
	}
	listen(socket_fd, 1);
	g_unix_fd_add(socket_fd, G_IO_IN, sock_handle, NULL);
	return 0;
}
#endif

int main(int argc, char** argv)
{
	sigset_t sigset, old_sigset;
	int ret = EXIT_SUCCESS;

	sigemptyset(&sigset);
	sigaddset(&sigset,SIGINT);
	sigaddset(&sigset,SIGHUP);
	sigaddset(&sigset,SIGTERM);
	sigaddset(&sigset,SIGUSR1);
	sigaddset(&sigset,SIGUSR2);
	sigprocmask(SIG_BLOCK, &sigset, &old_sigset);

	parse_command_line(argc, argv);

	/*
	 * Check if we are run under systemd and enable journal_logging
	 * and run in foreground if so. Systemd v232 or later will set
	 * INVOCATION_ID.
	 */
	if (getenv("INVOCATION_ID")) {
		journal_logging=1;
		foreground_mode=1;
	}

	/*
 	 * Open the syslog connection
 	 */
	openlog(argv[0], 0, LOG_DAEMON);

	if (getenv("IRQBALANCE_ONESHOT")) 
		one_shot_mode=1;

	if (getenv("IRQBALANCE_DEBUG")) {
		debug_mode=1;
		foreground_mode=1;
	}

	/*
 	 * If we are't in debug mode, don't dump anything to the console
 	 * note that everything goes to the console before we check this
 	 */
	if (journal_logging)
		log_indent = "....";
	else
		log_indent = "    ";

	if (!debug_mode)
		log_mask &= ~TO_CONSOLE;

	if (numa_available() > -1) {
		numa_avail = 1;
	} else 
		log(TO_CONSOLE, LOG_INFO, "This machine seems not NUMA capable.\n");

	if (geteuid() != 0)
		log(TO_ALL, LOG_WARNING, "Irqbalance hasn't been executed under root privileges, thus it won't in fact balance interrupts.\n");

	HZ = sysconf(_SC_CLK_TCK);
	if (HZ == -1) {
		log(TO_ALL, LOG_WARNING, "Unable to determine HZ defaulting to 100\n");
		HZ = 100;
	}
	
	if (!foreground_mode) {
		int pidfd = -1;
		if (daemon(0,0))
			exit(EXIT_FAILURE);
		/* Write pidfile which can be used to avoid starting multiple instances */
		if (pidfile && (pidfd = open(pidfile,
			O_WRONLY | O_CREAT | O_EXCL | O_TRUNC,
			S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) >= 0) {
			char str[16];
			snprintf(str, sizeof(str), "%u\n", getpid());
			write(pidfd, str, strlen(str));
			close(pidfd);
		}
	}

	build_object_tree();
	if (debug_mode)
		dump_object_tree();


	/* On single core UP systems irqbalance obviously has no work to do */
	if (num_online_cpus() <= 1) {
		char *msg = "Balancing is ineffective on systems with a "
			    "single cpu.  Shutting down\n";

		log(TO_ALL, LOG_WARNING, "%s", msg);
		goto out;
	}


	g_unix_signal_add(SIGINT, handler, NULL);
	g_unix_signal_add(SIGTERM, handler, NULL);
	g_unix_signal_add(SIGUSR1, handler, NULL);
	g_unix_signal_add(SIGUSR2, handler, NULL);
	g_unix_signal_add(SIGHUP, force_rescan, NULL);
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

#ifdef HAVE_IRQBALANCEUI
	if (init_socket()) {
		ret = EXIT_FAILURE;
		goto out;
	}
#endif
	if (init_thermal())
		log(TO_ALL, LOG_WARNING, "Failed to initialize thermal events.\n");
	main_loop = g_main_loop_new(NULL, FALSE);
	last_interval = sleep_interval;
	g_timeout_add_seconds(sleep_interval, scan, NULL);
	g_main_loop_run(main_loop);

	g_main_loop_quit(main_loop);

out:
	deinit_thermal();
	free_object_tree();
	free_cl_opts();
	free(polscript);

	/* Remove pidfile */
	if (!foreground_mode && pidfile)
		unlink(pidfile);
#ifdef HAVE_IRQBALANCEUI
	/* Remove socket */
	if (socket_fd > 0)
		close(socket_fd);
	if (socket_name[0])
		unlink(socket_name);
#endif
	return ret;
}
