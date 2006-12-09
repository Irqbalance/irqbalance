#ifndef _INCLUDE_GUARD_TYPES_H
#define _INCLUDE_GUARD_TYPES_H

#include <glib.h>

#include "cpumask.h"

#define	BALANCE_NONE		0
#define BALANCE_PACKAGE 	1
#define BALANCE_CACHE		2
#define BALANCE_CORE		3

#define IRQ_OTHER       0
#define IRQ_LEGACY      1
#define IRQ_SCSI        2
#define IRQ_TIMER       3
#define IRQ_ETH         4


struct package {
	uint64_t	workload;
	int	number;

	cpumask_t	mask;

	int class_count[7];

	GList	*cache_domains;
	GList 	*interrupts;
};

struct cache_domain {
	uint64_t	workload;
	int	number;

	int marker;

	cpumask_t	mask;

	cpumask_t	package_mask;

	int class_count[7];

	GList	*cpu_cores;
	GList 	*interrupts;
};


struct cpu_core {
	uint64_t	workload;
	int	number;

	int	marker;

	int class_count[7];

	cpumask_t	package_mask;
	cpumask_t	cache_mask;
	cpumask_t	mask;

	GList 	*interrupts;
};

struct interrupt {
	uint64_t	workload;

	int	balance_level;

	int	number;
	int	class;

	uint64_t	count;
	uint64_t	old_count;
	uint64_t	extra;

	cpumask_t	mask;
	cpumask_t	old_mask;
	

	cpumask_t	numa_mask;
};


#endif
