#ifndef __INCLUDE_GUARD_IRQBALANCE_H_
#define __INCLUDE_GUARD_IRQBALANCE_H_


#include "constants.h"

#include "cpumask.h"

#include <stdint.h>
#include <glib.h>

#include "types.h"
#include <numa.h>

struct interrupt;

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
extern GList *interrupts;

extern void parse_cpu_tree(void);
extern void clear_work_stats(void);
extern void parse_proc_interrupts(void);
extern void rebuild_irq_db(void);
extern void set_interrupt_count(int number, uint64_t count);
extern void set_msi_interrupt_numa(int number, char *devname);
extern int get_next_irq(int irq);
extern int find_irq_integer_prop(int irq, enum irq_prop prop);
extern cpumask_t find_irq_cpumask_prop(int irq, enum irq_prop prop);

extern void add_interrupt_numa(int number, cpumask_t mask, int node_num, int type);

void calculate_workload(void);
void reset_counts(void);
void dump_workloads(void);
void sort_irq_list(void);
void calculate_placement(void);
void dump_tree(void);

void activate_mapping(void);
void account_for_nic_stats(void);
void check_power_mode(void);
void clear_cpu_tree(void);
void pci_numa_scan(void);

#endif
