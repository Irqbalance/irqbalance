#ifndef __INCLUDE_GUARD_IRQBALANCE_H_
#define __INCLUDE_GUARD_IRQBALANCE_H_


#include "constants.h"

#include "cpumask.h"

#include <stdint.h>
#include <glib.h>
#include <syslog.h>
#include <limits.h>

#include "types.h"
#ifdef HAVE_NUMA_H
#include <numa.h>
#else
#define numa_available() -1
#endif

extern int package_count;
extern int cache_domain_count;
extern int core_count;
extern char *classes[];

extern void parse_cpu_tree(void);
extern void clear_work_stats(void);
extern void parse_proc_interrupts(void);
extern GList* collect_full_irq_list();
extern void parse_proc_stat(void);
extern void set_interrupt_count(int number, uint64_t count);
extern void set_msi_interrupt_numa(int number);

extern GList *rebalance_irq_list;

void update_migration_status(void);
void dump_workloads(void);
void sort_irq_list(GList **list);
void calculate_placement(void);
void dump_tree(void);

void activate_mappings(void);
void clear_cpu_tree(void);

/*===================NEW BALANCER FUNCTIONS============================*/

/*
 * Master topo_obj type lists
 */
extern GList *numa_nodes;
extern GList *packages;
extern GList *cache_domains;
extern GList *cpus;
extern int numa_avail;

enum hp_e {
	HINT_POLICY_IGNORE,
	HINT_POLICY_SUBSET,
	HINT_POLICY_EXACT
};

extern int debug_mode;
extern int one_shot_mode;
extern int need_rescan;
extern enum hp_e global_hint_policy;
extern unsigned long long cycle_count;
extern unsigned long power_thresh;
extern unsigned long deepest_cache;
extern char *banscript;
extern char *polscript;
extern cpumask_t banned_cpus;
extern cpumask_t unbanned_cpus;
extern long HZ;

/*
 * Numa node access routines
 */
extern void build_numa_node_list(void);
extern void free_numa_node_list(void);
extern void dump_numa_node_info(struct topo_obj *node, void *data);
extern void add_package_to_node(struct topo_obj *p, int nodeid);
extern struct topo_obj *get_numa_node(int nodeid);

/*
 * Package functions
 */
#define package_numa_node(p) ((p)->parent)

/*
 * cache_domain functions
 */
#define cache_domain_package(c) ((c)->parent)
#define cache_domain_numa_node(c) (package_numa_node(cache_domain_package((c))))

/*
 * cpu core functions
 */
#define cpu_cache_domain(cpu) ((cpu)->parent)
#define cpu_package(cpu) (cache_domain_package(cpu_cache_domain((cpu))))
#define cpu_numa_node(cpu) (package_numa_node(cache_domain_package(cpu_cache_domain((cpu)))))
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

extern unsigned int log_mask;
#define log(mask, lvl, fmt, args...) do {\
	if (log_mask & mask & TO_SYSLOG)\
		syslog(lvl, fmt, ##args);\
	if (log_mask & mask & TO_CONSOLE)\
		printf(fmt, ##args);\
}while(0)

#endif

