
#ifndef HELPERS_H
#define HELPERS_H

#include "irqbalance-ui.h"

extern GList *tree;


/* Helper functions */

gint sort_ints(gconstpointer First, gconstpointer Second);
gint sort_all_cpus(gconstpointer First, gconstpointer Second);
gint sort_all_irqs(gconstpointer First, gconstpointer Second);
char * hex_to_bitmap(char hex_digit);
gpointer copy_cpu_ban(gconstpointer src, gpointer data);
gpointer copy_irq(gconstpointer src, gpointer data);
void for_each_cpu(GList *list, void (*fp)(cpu_ban_t *cpu, void *data), void *data);
void for_each_int(GList *list, void (*fp)(int *number, void *data), void *data);
void for_each_irq(GList *list, void (*fp)(irq_t *irq, void *data), void *data);
void for_each_node(GList *list, void (*fp)(cpu_node_t *node, void *data), void *data);


/* Programmer debugging functions */

void dump_irq(irq_t *irq, void *data __attribute__((unused)));
void dump_node(cpu_node_t *node, void *data __attribute__((unused)));
void dump_tree();


#endif /* HELPERS_H */
