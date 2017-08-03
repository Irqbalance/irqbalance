
#include <glib.h>
#include <glib-unix.h>
#include <stdio.h>
#include <stdlib.h>
#include "helpers.h"
#include "ui.h"


gint sort_ints(gconstpointer First, gconstpointer Second)
{
	int *first = (int *)First;
	int *second = (int *)Second;
	if(*first < *second) {
		return -1;
	}
	if(*first == *second) {
		return 0;
	}
	if(*first > *second) {
		return 1;
	}
	return 1;
}

gint sort_all_cpus(gconstpointer First, gconstpointer Second)
{
	cpu_ban_t *first, *second;
	first = (cpu_ban_t *)First;
	second = (cpu_ban_t *)Second;

	if(first->number < second->number) {
		return -1;
	}
	if(first->number == second->number) {
		/* This should never happen */
		return 0;
	}
	if(first->number > second->number) {
		return 1;
	}
	return 1;
}

gint sort_all_irqs(gconstpointer First, gconstpointer Second)
{
	irq_t *first, *second;
	first = (irq_t *)First;
	second = (irq_t *)Second;

	if(first->vector < second->vector) {
		return -1;
	}
	if(first->vector == second->vector) {
		/* This should never happen */
		return 0;
	}
	if(first->vector > second->vector) {
		return 1;
	}
	return 1;
}

char * hex_to_bitmap(char hex_digit) {
	uint8_t digit = 0;
	if((hex_digit >= '0') && (hex_digit <= '9')) {
		digit = hex_digit - '0';
	} else if((hex_digit >= 'a') && (hex_digit <= 'f')) {
		digit = hex_digit - 'a' + 10;
	} else if((hex_digit >= 'A') && (hex_digit <= 'F')) {
		digit = hex_digit - 'A' + 10;
	} else {
		return "0000\0";
	}

	char *bitmap = malloc(5 * sizeof(char));
	bitmap[4] = '\0';
	int i;
	for(i = 3; i >= 0; i--) {
		bitmap[i] = digit % 2 ? '1' : '0';
		digit /= 2;
	}
	return bitmap;
}

gpointer copy_cpu_ban(gconstpointer src, gpointer data __attribute__((unused)))
{
	cpu_ban_t *old = (cpu_ban_t *)src; 
	cpu_ban_t *new = malloc(sizeof(cpu_ban_t));
	new->number = old->number;
	new->is_banned = old->is_banned;
	return new;
}

gpointer copy_irq(gconstpointer src, gpointer data __attribute__((unused)))
{
	irq_t *old = (irq_t *)src; 
	irq_t *new = malloc(sizeof(irq_t));
	new->vector = old->vector;
	new->load = old->load;
	new->diff = old->diff;
	new->is_banned = old->is_banned;
	new->class = old->class;
	new->assigned_to = g_list_copy(old->assigned_to);
	return new;
}

void for_each_cpu(GList *list, void (*fp)(cpu_ban_t *cpu, void *data), void *data)
{
	GList *entry;
	entry = g_list_first(list);
	while(entry) {
		fp(entry->data, data);
		entry = g_list_next(entry);
	}
}

void for_each_int(GList *list, void (*fp)(int *number, void *data), void *data)
{
	GList *entry;
	entry = g_list_first(list);
	while(entry) {
		fp(entry->data, data);
		entry = g_list_next(entry);
	}
}

void for_each_irq(GList *list, void (*fp)(irq_t *irq, void *data), void *data)
{
	GList *entry;
	entry = g_list_first(list);
	while(entry) {
		fp(entry->data, data);
		entry = g_list_next(entry);
	}
}

void for_each_node(GList *list, void (*fp)(cpu_node_t *node, void *data), void *data)
{
	GList *entry;
	entry = g_list_first(list);
	while(entry) {
		fp(entry->data, data);
		entry = g_list_next(entry);
	}
}

/* Programmer debugging functions */

void dump_irq(irq_t *irq, void *data __attribute__((unused)))
{
	printf("IRQ %d\n", irq->vector);
}

void dump_node(cpu_node_t *node, void *data __attribute__((unused)))
{
	printf("TYPE %d NUMBER %d\n", node->type, node->number);
	if(g_list_length(node->irqs) > 0) {
		for_each_irq(node->irqs, dump_irq, NULL);
	}
	if(g_list_length(node->children) > 0) {
		for_each_node(node->children, dump_node, NULL);
	}
}

void dump_tree()
{
	for_each_node(tree, dump_node, NULL);
}

