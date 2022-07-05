
#ifndef IRQBALANCE_UI_H
#define IRQBALANCE_UI_H

#include <stdio.h>
#include <stdint.h>
#include <glib.h>
#include <glib-unix.h>

#define SOCKET_PATH "irqbalance"
#define SOCKET_TMPFS "/run/irqbalance"

#define STATS "stats"
#define SET_SLEEP "settings sleep "
#define BAN_IRQS "settings ban irqs "
#define SETUP "setup"

/* IRQ CLASSES (same as irqbalance uses) */
#define IRQ_NODEF      -1
#define IRQ_OTHER       0
#define IRQ_LEGACY      1
#define IRQ_SCSI        2
#define IRQ_VIDEO       3
#define IRQ_ETH         4
#define IRQ_GBETH       5
#define IRQ_10GBETH     6
#define IRQ_VIRT_EVENT  7


/* Typedefs */

typedef enum node_type {
	OBJ_TYPE_CPU,
	OBJ_TYPE_CACHE,
	OBJ_TYPE_PACKAGE,
	OBJ_TYPE_NODE
} node_type_e;

typedef struct irq {
	int vector;
	uint64_t load;
	uint64_t diff;
	char is_banned;
	char is_changed;
	GList *assigned_to;
	int class;
} irq_t;

typedef struct cpu_node {
	node_type_e type;
	int number;
	uint64_t load;
	int is_powersave;
	struct cpu_node *parent;
	GList *children;
	GList *irqs;
	GList *cpu_list;
	char *cpu_mask;
} cpu_node_t;

typedef struct cpu_ban {
	int number;
	char is_banned;
	char is_changed;
} cpu_ban_t;

typedef struct setup {
	uint64_t sleep;
	GList *banned_irqs;
	GList *banned_cpus;
} setup_t;

/* Function prototypes */

struct msghdr * create_credentials_msg();
int init_connection();
void send_settings(char *data);
char * get_data(char *string);
void parse_setup(char *setup_data);
GList * concat_child_lists(cpu_node_t *node);
void copy_cpu_list_to_irq(irq_t *irq, void *data);
void assign_cpu_lists(cpu_node_t *node, void *data);
void assign_cpu_mask(cpu_node_t *node, void *data);
void parse_into_tree(char *data);
gboolean rescan_tree(gpointer data);
int main();


#endif /* IRQBALANCE_UI_H */
