
#ifndef UI_H
#define UI_H

#include <glib.h>
#include <glib-unix.h>
#include <curses.h>
#include <form.h>
#include <ncurses.h>
#include <signal.h>
#include "irqbalance-ui.h"
#include "helpers.h"

extern setup_t setup;

extern int offset;
extern int max_offset;

void show_frame(void);
void show_footer(void);

char * check_control_in_sleep_input(int max_len, int column_offest, int line_offset);
int get_valid_sleep_input(int column_offest);

void get_banned_cpu(int *cpu, char *data);
void print_cpu_line(cpu_ban_t *cpu, void *data);
void print_all_cpus(void);
void add_banned_cpu(int *banned_cpu, char *data);
void display_banned_cpus(void);
int toggle_cpu(GList *cpu_list, int cpu_number);
void get_new_cpu_ban_values(cpu_ban_t *cpu, void *data);
void get_cpu(cpu_node_t *node, void *data);
void handle_sleep_setting(void);
void handle_cpu_banning(void);

void copy_assigned_obj(int *number, char *data);
void print_assigned_objects_string(irq_t *irq, int *line_offset);
void print_irq_line(irq_t *irq, void *data);
void print_all_irqs(void);
int toggle_irq(GList *irq_list, int position);
void get_new_irq_ban_values(irq_t *irq, void *data);
void copy_irqs_from_nodes(cpu_node_t *node, void *data);
void get_all_irqs(void);
void handle_irq_banning(void);

void init(void);
void close_window(int sig);
void settings(void);
void setup_irqs(void);
void display_tree_node_irqs(irq_t *irq, void *data);
void display_tree_node(cpu_node_t *node, void *data);
void display_tree(void);


#endif /* UI_H */
