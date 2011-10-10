#ifndef _INCLUDE_GUARD_TYPES_H
#define _INCLUDE_GUARD_TYPES_H

#include <glib.h>

#include "cpumask.h"

#define	BALANCE_NONE		0
#define BALANCE_PACKAGE 	1
#define BALANCE_CACHE		2
#define BALANCE_CORE		3

/*
 * IRQ Classes
 */
#define IRQ_OTHER       0
#define IRQ_LEGACY      1
#define IRQ_SCSI        2
#define IRQ_TIMER       3
#define IRQ_ETH         4

/*
 * IRQ Types
 */
#define IRQ_TYPE_LEGACY 0
#define IRQ_TYPE_MSI	1
#define IRQ_TYPE_MSIX	2

struct common_obj_data {
	uint64_t load;
	int number;
	cpumask_t mask;
	GList *interrupts;
};

struct numa_node {
	struct common_obj_data common;
	GList	*packages;
};

struct package {
	struct common_obj_data common;
	struct numa_node *numa_node;
	GList	*cache_domains;
};

struct cache_domain {
	struct common_obj_data common;
	struct package *package;
	GList	*cpu_cores;
};


struct cpu_core {
	struct common_obj_data common;
	struct cache_domain *cache_domain;
	uint64_t irq_load;
	uint64_t softirq_load;
};

struct irq_info {
        int irq;
        int class;
        int type;
	int level;
        struct numa_node *numa_node;
        cpumask_t cpumask;
        cpumask_t affinity_hint;
        uint64_t irq_count;
        uint64_t last_irq_count;
	uint64_t load;
        int moved;
        struct common_obj_data *assigned_obj;
};

#endif
