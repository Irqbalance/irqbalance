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


/*
 * IRQ properties
 */
enum irq_prop {
	IRQ_CLASS = 0,
	IRQ_TYPE,
	IRQ_NUMA,
	IRQ_LEVEL,
	IRQ_LCPU_MASK,
	IRQ_INT_COUNT,
	IRQ_LAST_INT_COUNT,
	IRQ_MAX_PROPERTY
};

struct numa_node {
	uint64_t	workload;
	int	number;
	cpumask_t	mask;
	GList	*packages;
	GList	*interrupts;
};

struct package {
	uint64_t	workload;
	int	number;

	cpumask_t	mask;
	struct numa_node *numa_node;

	int class_count[7];

	GList	*cache_domains;
	GList 	*interrupts;
};

struct cache_domain {
	uint64_t	workload;
	int	number;

	int marker;
	int	node_num;

	struct package *package;

	cpumask_t	mask;

	int class_count[7];

	GList	*cpu_cores;
	GList 	*interrupts;
};


struct cpu_core {
	uint64_t	workload;
	int	number;

	int	marker;
	struct cache_domain *cache_domain;

	int class_count[7];

	cpumask_t	mask;

	GList 	*interrupts;
};

struct irq_info {
        int irq;
        int class;
        int type;
	int level;
        struct numa_node *numa_node;
        cpumask_t cpumask;
        cpumask_t affinity_hint;
	cpumask_t mask; /*this will go away soon*/
	cpumask_t old_mask; /*this will go away soon*/
        uint64_t irq_count;
        uint64_t last_irq_count;
	uint64_t workload;
        int moved;
        void *assigned_obj;
};

#endif
