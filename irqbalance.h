#ifndef __INCLUDE_GUARD_IRQBALANCE_H_
#define __INCLUDE_GUARD_IRQBALANCE_H_


#include "constants.h"

#include "cpumask.h"

#include <stdint.h>
#include <glib.h>
#include <glib-unix.h>
#include <syslog.h>
#include <limits.h>

#include "types.h"
#include "config.h"

#ifdef __aarch64__
#define AARCH64
#endif

#ifdef HAVE_NUMA_H
#include <numa.h>
#else
#define numa_available() -1
#endif

#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-journal.h>
#endif

#define	NUMA_NO_NODE (-1)

extern char *classes[];

extern void parse_cpu_tree(void);
extern void clear_work_stats(void);
extern void parse_proc_interrupts(void);
extern GList* collect_full_irq_list();
extern void parse_proc_stat(void);
extern void set_interrupt_count(int number, uint64_t count);
extern void set_msi_interrupt_numa(int number);
extern void init_irq_class_and_type(char *savedline, struct irq_info *info, int irq);
extern int proc_irq_hotplug(char *line, int irq, struct irq_info **pinfo);
extern void clear_no_existing_irqs(void);

extern GList *rebalance_irq_list;
extern void force_rebalance_irq(struct irq_info *info, void *data __attribute__((unused)));

void update_migration_status(void);
void dump_workloads(void);
void sort_irq_list(GList **list);
void calculate_placement(void);
void dump_tree(void);

void activate_mappings(void);
void clear_cpu_tree(void);
void free_cpu_topo(gpointer data);
/*===================NEW BALANCER FUNCTIONS============================*/

/*
 * Master topo_obj type lists
 */
extern GList *numa_nodes;
extern GList *packages;
extern GList *cache_domains;
extern GList *cpus;
extern int numa_avail;
extern GList *cl_banned_irqs;

extern int debug_mode;
extern int journal_logging;
extern int one_shot_mode;
extern int need_rescan;
extern int need_rebuild;
extern unsigned long long cycle_count;
extern unsigned long power_thresh;
extern unsigned long deepest_cache;
extern char *polscript;
extern cpumask_t banned_cpus;
extern cpumask_t unbanned_cpus;
extern long HZ;
extern unsigned long migrate_ratio;

/*
 * Numa node access routines
 */
extern void build_numa_node_list(void);
extern void free_numa_node_list(void);
extern void dump_numa_node_info(struct topo_obj *node, void *data);
extern void connect_cpu_mem_topo(struct topo_obj *p, void *data);
extern struct topo_obj *get_numa_node(int nodeid);

/*
 * cpu core functions
 */
#define cpu_numa_node(cpu) ((cpu)->parent->numa_nodes)
extern struct topo_obj *find_cpu_core(int cpunr);
extern int get_cpu_count(void);

/*
 * irq db functions
 */
extern void rebuild_irq_db(void);
extern void free_irq_db(void);
extern void add_cl_banned_irq(int irq);
extern void for_each_irq(GList *list, void (*cb)(struct irq_info *info,  void *data), void *data);
extern struct irq_info *get_irq_info(int irq);
extern void migrate_irq(GList **from, GList **to, struct irq_info *info);
extern void free_cl_opts(void);
extern void add_cl_banned_module(char *modname);
#define irq_numa_node(irq) ((irq)->numa_node)


/*
 * Generic object functions
 */
static inline void for_each_object(GList *list, void (*cb)(struct topo_obj *obj,  void *data), void *data)
{
	GList *entry, *next;
	entry = g_list_first(list);
	while (entry) {
		next = g_list_next(entry);
		cb(entry->data, data);
		entry = next;
	}
}

/*
 * Logging functions
 */
#define TO_SYSLOG	(1 << 0)
#define TO_CONSOLE	(1 << 1)
#define TO_ALL		(TO_SYSLOG | TO_CONSOLE)

extern const char * log_indent;
extern unsigned int log_mask;
#ifdef HAVE_LIBSYSTEMD
#define log(mask, lvl, fmt, args...) do {					\
	if (journal_logging) {							\
		sd_journal_print(lvl, fmt, ##args);				\
		if (log_mask & mask & TO_CONSOLE)				\
			printf(fmt, ##args);					\
	} else { 								\
		if (log_mask & mask & TO_SYSLOG) 				\
			syslog(lvl, fmt, ##args); 				\
		if (log_mask & mask & TO_CONSOLE) 				\
			printf(fmt, ##args); 					\
	} 									\
}while(0)
#else /* ! HAVE_LIBSYSTEMD */
#define log(mask, lvl, fmt, args...) do {					\
	if (journal_logging) {							\
		printf("<%d>", lvl); 						\
		printf(fmt, ##args);						\
	} else { 								\
		if (log_mask & mask & TO_SYSLOG) 				\
			syslog(lvl, fmt, ##args); 				\
		if (log_mask & mask & TO_CONSOLE) 				\
			printf(fmt, ##args); 					\
	} 									\
}while(0)
#endif /* HAVE_LIBSYSTEMD */

#define SOCKET_PATH "irqbalance"
#define SOCKET_TMPFS "/run/irqbalance"

extern int process_one_line(char *path, void (*cb)(char *line, void *data), void *data);
extern void get_mask_from_bitmap(char *line, void *mask);
extern void get_int(char *line, void *data);
extern void get_hex(char *line, void *data);

#endif /* __INCLUDE_GUARD_IRQBALANCE_H_ */

