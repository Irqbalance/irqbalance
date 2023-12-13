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
#define IRQ_NODEF	-1
#define IRQ_OTHER       0
#define IRQ_LEGACY      1
#define IRQ_SCSI        2
#define IRQ_VIDEO       3
#define IRQ_ETH         4
#define IRQ_GBETH       5
#define IRQ_10GBETH     6
#define IRQ_VIRT_EVENT  7

/*
 * IRQ Types
 */
#define IRQ_TYPE_LEGACY     0
#define IRQ_TYPE_MSI        1
#define IRQ_TYPE_MSIX       2
#define IRQ_TYPE_VIRT_EVENT 3

/*
 * IRQ Internal tracking flags
 */
#define IRQ_FLAG_BANNED                 (1ULL << 0)

enum obj_type_e {
	OBJ_TYPE_CPU,
	OBJ_TYPE_CACHE,
	OBJ_TYPE_PACKAGE,
	OBJ_TYPE_NODE
};

struct topo_obj {
	uint64_t load;
	uint64_t last_load;
	uint64_t irq_count;
	enum obj_type_e obj_type;
	int number;
	int powersave_mode;
	cpumask_t mask;
	GList *interrupts;
	struct topo_obj *parent;
	GList *children;
	GList *numa_nodes;
	GList **obj_type_list;
};

struct irq_info {
	int irq;
	int class;
	int type;
	int level;
	int flags;
	struct topo_obj *numa_node;
	cpumask_t cpumask;
	uint64_t irq_count;
	uint64_t last_irq_count;
	uint64_t load;
	int moved;
	int existing;
	struct topo_obj *assigned_obj;
	char *name;
};

#endif
