#ifndef __INCLUDE_GUARD_IRQBALANCE_H_
#define __INCLUDE_GUARD_IRQBALANCE_H_


#include "constants.h"

#include "cpumask.h"

#include <stdint.h>
#include <glib.h>

#include "types.h"
#include <numa.h>

extern int package_count;
extern int cache_domain_count;
extern int core_count;
extern char *classes[];
extern int map_class_to_level[7];
extern int class_counts[7];
extern int debug_mode;
extern int power_mode;
extern int need_cpu_rescan;
extern int one_shot_mode;

extern void parse_cpu_tree(void);
extern void clear_work_stats(void);
extern void parse_proc_interrupts(void);
extern void parse_proc_stat(void);
extern void set_interrupt_count(int number, uint64_t count);
extern void set_msi_interrupt_numa(int number);

extern GList *rebalance_irq_list;

void update_migration_status(void);
void reset_counts(void);
void dump_workloads(void);
void sort_irq_list(GList **list);
void calculate_placement(void);
void dump_tree(void);

void activate_mappings(void);
void account_for_nic_stats(void);
void check_power_mode(void);
void clear_cpu_tree(void);
void pci_numa_scan(void);

/*===================NEW BALANCER FUNCTIONS============================*/
/*
 * Numa node access routines
 */
extern void build_numa_node_list(void);
extern void free_numa_node_list(void);
extern void dump_numa_node_info(struct common_obj_data *node, void *data);
extern void for_each_numa_node(GList *list, void (*cb)(struct common_obj_data *node, void *data), void *data);
extern void add_package_to_node(struct package *p, int nodeid);
extern struct numa_node *get_numa_node(int nodeid);

/*
 * Package functions
 */
#define package_numa_node(p) ((p)->numa_node)
extern void for_each_package(GList *list, void (*cb)(struct common_obj_data *p, void *data), void *data);

/*
 * cache_domain functions
 */
#define cache_domain_package(c) ((c)->package)
#define cache_domain_numa_node(c) (package_numa_node(cache_domain_package((c))))
extern void for_each_cache_domain(GList *list, void (*cb)(struct common_obj_data *c, void *data), void *data);

/*
 * cpu core functions
 */
#define cpu_cache_domain(cpu) ((cpu)->cache_domain)
#define cpu_package(cpu) (cache_domain_package(cpu_cache_domain((cpu))))
#define cpu_numa_node(cpu) (package_numa_node(cache_domain_package(cpu_cache_domain((cpu)))))
extern void for_each_cpu_core(GList *list, void (*cb)(struct common_obj_data *c, void *data), void *data);
extern struct cpu_core *find_cpu_core(int cpunr);
extern int get_cpu_count(void);

/*
 * irq db functions
 */
extern void rebuild_irq_db(void);
extern void free_irq_db(void);
extern void for_each_irq(GList *list, void (*cb)(struct irq_info *info,  void *data), void *data);
extern struct irq_info *get_irq_info(int irq);
extern void migrate_irq(GList **from, GList **to, struct irq_info *info);
extern struct irq_info *add_misc_irq(int irq);

#define irq_numa_node(irq) ((irq)->numa_node)

#endif

