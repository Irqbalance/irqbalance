
#include <inttypes.h>
#include <string.h>
#include "ui.h"

int offset;
int max_offset;

GList *all_cpus = NULL;
GList *all_irqs = NULL;

static char **irq_name;

char *IRQ_CLASS_TO_STR[] = {
			"Other",
			"Legacy",
			"SCSI",
			"Video",
			"Ethernet",
			"Gigabit Ethernet",
			"10-Gigabit Ethernet",
			"Virt Event"};

void show_frame()
{
	int i;
	attrset(COLOR_PAIR(4));
	char top[COLS];
	top[0] = '\0';
	while(strlen(top) != (size_t)COLS - 1) {
		snprintf(top + strlen(top), COLS - strlen(top), " ");
	}
	mvprintw(0, 0, "%s", top);
	for(i = 0; i < LINES; i++) {
		mvprintw(i, 0, " ");
		mvprintw(i, COLS - 1, " ");
	}
}

void show_footer()
{
	char footer[COLS];
	snprintf(footer, COLS - 1,
		" q (QUIT)   F3 (TREE)   F4 (SETTINGS)   F5 (SETUP IRQS)");
	while(strlen(footer) != (size_t)COLS - 1) {
		snprintf(footer + strlen(footer), COLS - strlen(footer), " ");
	}
	attrset(COLOR_PAIR(4));
	mvprintw(LINES - 1, 0, "%s", footer);
}

char * check_control_in_sleep_input(int max_len, int column_offest, int line_offset)
{
	char *input_to = malloc(max_len * sizeof(char));
	int iteration = 0;
	while(iteration < max_len) {
		int new = getch();
		switch(new) {
		case ERR:
			/* No input is ready for nonblocking getch() call */
			break;
		case '\r':
		case '\n':
			input_to[iteration] = '\0';
			return input_to;
		case 'q':
			close_window(0);
			break;
		case KEY_BACKSPACE:
			if(iteration > 0) {
				attrset(COLOR_PAIR(5));
				iteration--;
				mvaddch(line_offset, column_offest + iteration, ' ');
			}
			move(line_offset, column_offest + iteration);
			attrset(COLOR_PAIR(6));
			break;
		case 27:
			free(input_to);
			return NULL;
		default:
			input_to[iteration] = new;
			iteration++;
			break;
		}
	}
	return input_to;
}

int get_valid_sleep_input(int column_offest)
{
	uint64_t new_sleep = setup.sleep;
	while(1) {
		attrset(COLOR_PAIR(5));
		mvprintw(2, column_offest, "			");
		attrset(COLOR_PAIR(6));
		refresh();
		move(2, column_offest);
		curs_set(1);
		char *input = check_control_in_sleep_input(20, column_offest, 3);
		if(input == NULL) {
			curs_set(0);
			attrset(COLOR_PAIR(1));
			mvprintw(2, column_offest, "%lu			", new_sleep);
			move(LINES, COLS);
			break;
		}
		attrset(COLOR_PAIR(1));
		mvprintw(LINES - 2, 1, "							");
		curs_set(0);
		refresh();
		char *error;
		new_sleep = strtol(input, &error, 10);
		if((*error == '\0') && (new_sleep >= 1)) {
			break;
		} else {
			new_sleep = setup.sleep;
			attrset(COLOR_PAIR(4));
			mvprintw(LINES - 2, 1,
				"Invalid input: %s								",
				input);
			refresh();
		}
		free(input);
	}

	attrset(COLOR_PAIR(1));
	mvprintw(2, column_offest, "%lu				", new_sleep);

	return new_sleep;
}

void get_banned_cpu(int *cpu, void *data __attribute__((unused)))
{
	cpu_ban_t *new = malloc(sizeof(cpu_ban_t));
	new->number = *cpu;
	new->is_banned = 1;
	all_cpus = g_list_append(all_cpus, new);
}

void print_tmp_cpu_line(cpu_ban_t *cpu, void *data __attribute__((unused)))
{
	int line = max_offset - offset + 6;
	if (max_offset >= offset && line < LINES - 3) {
		if (cpu->is_changed)
			attrset(COLOR_PAIR(3));
		else if(cpu->is_banned)
			attrset(COLOR_PAIR(10));
		else
			attrset(COLOR_PAIR(9));
		mvprintw(line, 3, "CPU %d     ", cpu->number);
		mvprintw(line, 19, "%s", cpu->is_banned ?
				"YES	" :
				"NO	 ");
	}
	max_offset++;
}

void print_cpu_line(cpu_ban_t *cpu, void *data __attribute__((unused)))
{
	int line = max_offset - offset + 6;
	if (max_offset >= offset && line < LINES - 2) {
		if(cpu->is_banned)
			attrset(COLOR_PAIR(10));
		else
			attrset(COLOR_PAIR(9));
		mvprintw(line, 3, "CPU %d     ", cpu->number);
		mvprintw(line, 19, "%s", cpu->is_banned ?
				"YES	" :
				"NO	 ");
	}
	max_offset++;
}

void print_all_cpus()
{
	max_offset = 0;
	if(all_cpus == NULL) {
		for_each_node(tree, get_cpu, NULL);
		for_each_int(setup.banned_cpus, get_banned_cpu, NULL);
		all_cpus = g_list_sort(all_cpus, sort_all_cpus);
	}
	attrset(COLOR_PAIR(2));
	mvprintw(4, 3, "NUMBER          IS BANNED");
	for_each_cpu(all_cpus, print_cpu_line, NULL);
	max_offset -= LINES - 8;
	if (max_offset < 0)
		max_offset = 0;
}

void add_banned_cpu(int *banned_cpu, void *data)
{
	snprintf(data + strlen(data), 1024 - strlen(data), "%d, ", *banned_cpu);
}

void display_banned_cpus()
{
	char banned_cpus[1024] = "Banned CPU numbers: \0";
	if(g_list_length(setup.banned_cpus) > 0) {
		for_each_int(setup.banned_cpus, add_banned_cpu, banned_cpus);
		snprintf(banned_cpus + strlen(banned_cpus) - 2,
				1024 - strlen(banned_cpus), "\n");
	} else {
		snprintf(banned_cpus + strlen(banned_cpus),
				1024 - strlen(banned_cpus), "None\n");
	}
	attrset(COLOR_PAIR(0));
	mvprintw(2, 5, "%s\n", banned_cpus);
}

int toggle_cpu(GList *cpu_list, int cpu_number)
{
	GList *entry = g_list_first(cpu_list);
	cpu_ban_t *entry_data = (cpu_ban_t *)(entry->data);
	while(entry_data->number != cpu_number) {
		entry = g_list_next(entry);
		entry_data = (cpu_ban_t *)(entry->data);
	}
	if(((cpu_ban_t *)(entry->data))->is_banned) {
		((cpu_ban_t *)(entry->data))->is_banned = 0;
	} else {
		((cpu_ban_t *)(entry->data))->is_banned = 1;
	}
	((cpu_ban_t *)(entry->data))->is_changed = 1;
	return ((cpu_ban_t *)(entry->data))->is_banned;
}

void get_new_cpu_ban_values(cpu_ban_t *cpu, void *data)
{
	char *mask_data = (char *)data;
	if(cpu->is_banned) {
		snprintf(mask_data + strlen(mask_data), 1024 - strlen(mask_data),
				"%d,", cpu->number);
	}
}

void get_cpu(cpu_node_t *node, void *data __attribute__((unused)))
{
	if(node->type == OBJ_TYPE_CPU) {
		cpu_ban_t *new = malloc(sizeof(cpu_ban_t));
		new->number = node->number;
		new->is_banned = 0;
		all_cpus = g_list_append(all_cpus, new);
	}
	if(g_list_length(node->children) > 0) {
		for_each_node(node->children, get_cpu, NULL);
	}
}

void handle_cpu_banning()
{
	GList *tmp = g_list_copy_deep(all_cpus, copy_cpu_ban, NULL);
	attrset(COLOR_PAIR(5));
	mvprintw(LINES - 3, 1, "Move up and down the list, toggle ban with Enter.");
	mvprintw(LINES - 2, 1,
			"Press ESC for discarding and <S> for saving the values.");
	move(6, 19);
	curs_set(1);
	refresh();
	size_t position = 6;
	char processing = 1;
	while(processing) {
		int direction = getch();
		switch(direction) {
		case KEY_UP:
			if(position > 6) {
				position--;
				move(position, 19);
			} else if (offset > 0) {
				offset--;
				max_offset = 0;
				for_each_cpu(tmp, print_tmp_cpu_line, NULL);
				max_offset -= LINES - 9;
				if (max_offset < 0)
					max_offset = 0;
				move(position, 19);
			}
			break;
		case KEY_DOWN:
			if(position < (size_t)(LINES - 4)) {
				if (position <= g_list_length(all_cpus) + 4 - offset) {
					position++;
					move(position, 19);
				}
			} else if (offset < max_offset) {
				offset++;
				max_offset = 0;
				for_each_cpu(tmp, print_tmp_cpu_line, NULL);
				max_offset -= LINES - 9;
				if (max_offset < 0)
					max_offset = 0;
				move(position, 19);
			}
			break;
		case '\n':
		case '\r': {
			attrset(COLOR_PAIR(3));
			int banned = toggle_cpu(tmp, position + offset - 6);
			mvprintw(position, 3, "CPU %d     ", position + offset - 6);
			if(banned) {
				mvprintw(position, 19, "YES");
			} else {
				mvprintw(position, 19, "NO ");
			}
			move(position, 19);
			refresh();
			break;
		}
		case 27:
			processing = 0;
			curs_set(0);
			g_list_free(tmp);
			print_all_cpus();
			attrset(COLOR_PAIR(0));
			mvprintw(LINES - 3, 1, "			\
														");
			attrset(COLOR_PAIR(5));
			mvprintw(LINES - 2, 1,
				"Press <S> for changing sleep setup, <C> for CPU ban setup.  ");
			show_frame();
			show_footer();
			refresh();
			break;
		case 's':
			processing = 0;
			g_list_free(all_cpus);
			all_cpus = tmp;
			curs_set(0);
			print_all_cpus();
			attrset(COLOR_PAIR(0));
			mvprintw(LINES - 3, 1, "			\
														");
			attrset(COLOR_PAIR(5));
			mvprintw(LINES - 2, 1,
				"Press <S> for changing sleep setup, <C> for CPU ban setup.  ");
			show_frame();
			show_footer();
			refresh();
			char settings_string[1024] = "settings cpus \0";
			for_each_cpu(all_cpus, get_new_cpu_ban_values, settings_string);
			if(!strcmp("settings cpus \0", settings_string)) {
				strncpy(settings_string + strlen(settings_string),
						"NULL", 1024 - strlen(settings_string));
			}
			send_settings(settings_string);
			break;
		case 'q':
			processing = 0;
			close_window(0);
			break;
		default:
			break;
		}
	}
}

static int rbot, rtop;

static inline void bsnl_emit(char *buf, int buflen)
{
	int len = strlen(buf);
	if (len > 0) {
		snprintf(buf + len, buflen - len, ",");
		len++;
	}
	if (rbot == rtop)
		snprintf(buf + len, buflen - len, "%d", rbot);
	else
		snprintf(buf + len, buflen - len, "%d-%d", rbot, rtop);
}

void copy_assigned_obj(int *number, void *data)
{
	if (rtop == -1) {
		rbot = rtop = *number;
		return;
	}
	if (*number > rtop + 1) {
		bsnl_emit(data, 128);
		rbot = *number;
	}
	rtop = *number;
}

void print_assigned_objects_string(irq_t *irq, int *line_offset)
{
	if(irq->is_banned) {
		return;
	}
	char assigned_to[128] = "\0";
	rtop = -1;
	for_each_int(irq->assigned_to, copy_assigned_obj, assigned_to);
	bsnl_emit(assigned_to, 128);
	mvprintw(*line_offset, 68, "%s             ", assigned_to);
}

void get_irq_name(int end)
{
	int i, cpunr, len;
	FILE *output;
	char *cmd;
	char buffer[128];

	if (irq_name == NULL) {
		irq_name = malloc(sizeof(char *) * LINES);
		for (i = 4; i < LINES; i++) {
			irq_name[i] = malloc(sizeof(char) * 50);
			memset(irq_name[i], 0, sizeof(char) * 50);
		}
	}

	output = popen("cat /proc/interrupts | head -1 | awk '{print NF}'", "r");
	if (!output)
		return;
	fscanf(output, "%d", &cpunr);
	pclose(output);

	len = snprintf(NULL, 0, "cat /proc/interrupts | awk '{for (i=%d;i<=NF;i++)printf(\"%%s \", $i);print \"\"}' | cut -c-49", cpunr + 2);
	cmd = alloca(sizeof(char) * (len + 1));
	snprintf(cmd, len + 1, "cat /proc/interrupts | awk '{for (i=%d;i<=NF;i++)printf(\"%%s \", $i);print \"\"}' | cut -c-49", cpunr + 2);
	output = popen(cmd, "r");
	for (i = 0; i <= offset; i++)
		fgets(buffer, 50, output);
	for (i = 4; i < end; i++)
		fgets(irq_name[i], 50, output);
	pclose(output);
}

void print_tmp_irq_line(irq_t *irq, void *data __attribute__((unused)))
{
	int line = max_offset - offset + 4;
	max_offset++;
	if (line < 4 || line >= LINES - 3)
		return;
	switch(irq->class) {
	case(IRQ_OTHER):
		attrset(COLOR_PAIR(1));
		break;
	case(IRQ_LEGACY):
		attrset(COLOR_PAIR(2));
		break;
	case(IRQ_SCSI):
		attrset(COLOR_PAIR(3));
		break;
	case(IRQ_VIDEO):
		attrset(COLOR_PAIR(8));
		break;
	case(IRQ_ETH):
	case(IRQ_GBETH):
	case(IRQ_10GBETH):
		attrset(COLOR_PAIR(9));
		break;
	case(IRQ_VIRT_EVENT):
		attrset(COLOR_PAIR(10));
		break;
	default:
		attrset(COLOR_PAIR(0));
		break;
	}
	mvprintw(line, 3, "IRQ %d      ", irq->vector);
	mvprintw(line, 19, "%s", irq->is_banned ? "YES" : "NO ");
	mvprintw(line, 36, "%s               ",
			 irq->class < 0 ? "Unknown" : IRQ_CLASS_TO_STR[irq->class]);
	print_assigned_objects_string(irq, &line);
	mvprintw(line, 120, "%s", irq_name[line]);
}

void print_irq_line(irq_t *irq, void *data __attribute__((unused)))
{
	int line = max_offset - offset + 4;
	max_offset++;
	if (line < 4 || line >= LINES - 2)
		return;
	switch(irq->class) {
	case(IRQ_OTHER):
		attrset(COLOR_PAIR(1));
		break;
	case(IRQ_LEGACY):
		attrset(COLOR_PAIR(2));
		break;
	case(IRQ_SCSI):
		attrset(COLOR_PAIR(3));
		break;
	case(IRQ_VIDEO):
		attrset(COLOR_PAIR(8));
		break;
	case(IRQ_ETH):
	case(IRQ_GBETH):
	case(IRQ_10GBETH):
		attrset(COLOR_PAIR(9));
		break;
	case(IRQ_VIRT_EVENT):
		attrset(COLOR_PAIR(10));
		break;
	default:
		attrset(COLOR_PAIR(0));
		break;
	}
	mvprintw(line, 3, "IRQ %d", irq->vector);
	mvprintw(line, 19, "%s", irq->is_banned ? "YES" : "NO ");
	mvprintw(line, 36, "%s               ",
			 irq->class < 0 ? "Unknown" : IRQ_CLASS_TO_STR[irq->class]);
	print_assigned_objects_string(irq, &line);
	mvprintw(line, 120, "%s", irq_name[line]);
}

void print_all_irqs()
{
	max_offset = 0;
	attrset(COLOR_PAIR(0));
	mvprintw(2, 3,
			"NUMBER          IS BANNED        CLASS      \
			    ASSIGNED TO CPUS                                    IRQ NAME");
	get_irq_name(LINES - 2);
	for_each_irq(all_irqs, print_irq_line, NULL);
	max_offset -= LINES - 6;
	if (max_offset < 0)
		max_offset = 0;
}

int toggle_irq(GList *irq_list, int position)
{
	GList *entry = g_list_first(irq_list);
	int irq_node = 0;
	while(irq_node != position) {
		entry = g_list_next(entry);
		irq_node++;
	}
	if(((irq_t *)(entry->data))->is_banned) {
		((irq_t *)(entry->data))->is_banned = 0;
	} else {
		((irq_t *)(entry->data))->is_banned = 1;
	}
	((irq_t *)(entry->data))->is_changed = 1;
	return ((irq_t *)(entry->data))->is_banned;
}

void get_new_irq_ban_values(irq_t *irq, void *data)
{
	char *ban_list = (char *)data;
	if(irq->is_banned) {
		snprintf(ban_list + strlen(ban_list), 1024 - strlen(ban_list),
				" %d", irq->vector);
	}
}

void copy_irqs_from_nodes(cpu_node_t *node, void *data __attribute__((unused)))
{
	if(g_list_length(node->irqs) > 0) {
		GList *new = g_list_copy_deep(node->irqs, copy_irq, NULL);
		all_irqs = g_list_concat(all_irqs, new);
	}
	if(g_list_length(node->children) > 0) {
		for_each_node(node->children, copy_irqs_from_nodes, all_irqs);
	}
}

void get_all_irqs()
{
	all_irqs = g_list_copy_deep(setup.banned_irqs, copy_irq, NULL);
	for_each_node(tree, copy_irqs_from_nodes, NULL);
}

void handle_irq_banning()
{
	GList *tmp = g_list_copy_deep(all_irqs, copy_irq, NULL);
	attrset(COLOR_PAIR(5));
	mvprintw(LINES - 3, 1, "Move up and down the list, toggle ban with Enter.");
	mvprintw(LINES - 2, 1, "Press ESC for discarding and <S> for saving the values.");
	move(4, 19);
	curs_set(1);
	refresh();
	size_t position = 4;
	char processing = 1;
	while(processing) {
		int direction = getch();
		switch(direction) {
		case KEY_UP:
			if(position > 4) {
				position--;
				move(position, 19);
			} else if (offset > 0) {
				offset--;
				max_offset = 0;
				get_irq_name(LINES - 3);
				for_each_irq(tmp, print_tmp_irq_line, NULL);
				max_offset -= LINES - 7;
				if (max_offset < 0)
					max_offset = 0;
				move(position, 19);
			}
			break;
		case KEY_DOWN:
			if (position < (size_t)(LINES  - 4)) {
				if(position < g_list_length(all_irqs) + 3) {
					position++;
					move(position, 19);
				}
			} else if (offset < max_offset) {
				offset++;
				max_offset = 0;
				get_irq_name(LINES - 3);
				for_each_irq(tmp, print_tmp_irq_line, NULL);
				max_offset -= LINES - 7;
				if (max_offset < 0)
					max_offset = 0;
				move(position, 19);
			}
			break;
		case '\n':
		case '\r': {
			attrset(COLOR_PAIR(3));
			int banned = toggle_irq(tmp, position + offset - 4);
			if(banned) {
				mvprintw(position, 19, "YES");
			} else {
				mvprintw(position, 19, "NO ");
			}
			move(position, 19);
			refresh();
			break;
		}
		case 27:
			processing = 0;
			curs_set(0);
			/* Forget the changes */
			g_list_free(tmp);
			print_all_irqs();
			attrset(COLOR_PAIR(0));
			mvprintw(LINES - 3, 1, "			\
					");
			attrset(COLOR_PAIR(5));
			mvprintw(LINES - 2, 1, "Press <I> for setting up IRQ banning.\
				");
			show_frame();
			show_footer();
			refresh();
			break;
		case 's':
			processing = 0;
			g_list_free(all_irqs);
			all_irqs = tmp;
			curs_set(0);
			print_all_irqs();
			attrset(COLOR_PAIR(0));
			mvprintw(LINES - 3, 1, "			\
					");
			attrset(COLOR_PAIR(5));
			mvprintw(LINES - 2, 1, "Press <I> for setting up IRQ banning.\
				");
			attrset(COLOR_PAIR(3));
			show_frame();
			show_footer();
			refresh();
			char settings_string[1024] = BAN_IRQS;
			for_each_irq(all_irqs, get_new_irq_ban_values, settings_string);
			if(!strcmp(BAN_IRQS, settings_string)) {
				strncpy(settings_string + strlen(settings_string),
						" NONE", 1024 - strlen(settings_string));
			}
			send_settings(settings_string);
			break;
		case 'q':
			processing = 0;
			close_window(0);
			break;
		default:
			break;
		}
	}
}

void handle_sleep_setting()
{
	char info[128] = "Current sleep interval between rebalancing: \0";
	uint8_t sleep_input_offset = strlen(info) + 3;
	mvprintw(LINES - 1, 1, "Press ESC for discarding your input.\
												");
	attrset(COLOR_PAIR(0));
	mvprintw(LINES - 2, 1, "			\
												");
	uint64_t new_sleep = get_valid_sleep_input(sleep_input_offset);
	if(new_sleep != setup.sleep) {
		setup.sleep = new_sleep;
		char settings_data[128];
		snprintf(settings_data, 128, "%s %" PRIu64, SET_SLEEP, new_sleep);
		send_settings(settings_data);
	}
	attrset(COLOR_PAIR(5));
	mvprintw(LINES - 2, 1, "Press <S> for changing sleep setup, <C> for CPU ban setup. ");
	show_frame();
	show_footer();
	refresh();
}

void init()
{
	signal(SIGINT, close_window);
	initscr();
	keypad(stdscr, TRUE);
	curs_set(0);
	nonl();
	cbreak();
	nodelay(stdscr, TRUE);
	echo();
	if(has_colors()) {
		start_color();
		init_pair(1, COLOR_RED, COLOR_BLACK);
		init_pair(2, COLOR_YELLOW, COLOR_BLACK);
		init_pair(3, COLOR_GREEN, COLOR_BLACK);
		init_pair(4, COLOR_WHITE, COLOR_BLUE);
		init_pair(5, COLOR_WHITE, COLOR_RED);
		init_pair(6, COLOR_RED, COLOR_WHITE);
		init_pair(7, COLOR_BLACK, COLOR_CYAN);
		init_pair(8, COLOR_BLUE, COLOR_BLACK);
		init_pair(9, COLOR_CYAN, COLOR_BLACK);
		init_pair(10, COLOR_MAGENTA, COLOR_BLACK);
	}

	offset = 0;
	display_tree();
}

void close_window(int sig __attribute__((unused)))
{
	g_list_free(all_cpus);
	g_list_free(setup.banned_irqs);
	g_list_free(setup.banned_cpus);
	g_list_free_full(tree, free);
	endwin();
	exit(EXIT_SUCCESS);
}

void settings()
{
	clear();
	char *setup_data = get_data(SETUP);
	parse_setup(setup_data);

	char info[128] = "Current sleep interval between rebalancing: \0";
	snprintf(info + strlen(info), 128 - strlen(info), "%" PRIu64 "\n", setup.sleep);
	attrset(COLOR_PAIR(1));
	mvprintw(2, 3, "%s", info);
	print_all_cpus();
	attrset(COLOR_PAIR(5));
	mvprintw(LINES - 2, 1, "Press <S> for changing sleep setup, <C> for CPU ban setup. ");
	show_frame();
	show_footer();
	refresh();
	free(setup_data);
}

void setup_irqs()
{
	clear();
	get_all_irqs();
	all_irqs = g_list_sort(all_irqs, sort_all_irqs);
	print_all_irqs();
	attrset(COLOR_PAIR(5));
	mvprintw(LINES - 2, 1, "Press <I> for setting up IRQ banning.");
	show_frame();
	show_footer();
	refresh();
}

void display_tree_node_irqs(irq_t *irq, void *data)
{
	char indent[32] = "	   \0";
	if (max_offset >= offset && max_offset - offset < LINES - 5) {
		snprintf(indent + strlen(indent), 32 - strlen(indent), "%s", (char *)data);
		attrset(COLOR_PAIR(3));
		printw("%sIRQ %u, IRQs since last rebalance %lu\n",
			indent, irq->vector, irq->diff);
	}
	max_offset++;
}

void display_tree_node(cpu_node_t *node, void *data)
{
	int i;
	const char *node_type_to_str[] = {
			"CPU\0",
			"CACHE DOMAIN\0",
			"CPU PACKAGE\0",
			"NUMA NODE\0"};

	char *spaces = "    \0";
	char indent[32] = "\0";
	char *asciitree = " `--\0";
	for(i = node->type; i <= OBJ_TYPE_NODE; i++) {
		snprintf(indent + strlen(indent), 32 - strlen(indent), "%s", spaces);
		if(i != OBJ_TYPE_NODE) {
			snprintf(indent + strlen(indent), 32 - strlen(indent), "   ");
		}
	}
	snprintf(indent + strlen(indent), 32 - strlen(indent), "%s", asciitree);
	char copy_to[1024];
	char *numa_available = "\0";
	if((node->type == OBJ_TYPE_NODE) && (node->number == -1)) {
		numa_available = " (This machine is not NUMA-capable)";
	}
	snprintf(copy_to, 1024, "%s%s, number %d%s, CPU mask %s\n",
			indent, node_type_to_str[node->type], node->number, numa_available,
			node->cpu_mask);
	switch(node->type) {
	case(OBJ_TYPE_CPU):
		attrset(COLOR_PAIR(1));
		break;
	case(OBJ_TYPE_CACHE):
		attrset(COLOR_PAIR(2));
		break;
	case(OBJ_TYPE_PACKAGE):
		attrset(COLOR_PAIR(8));
		break;
	case(OBJ_TYPE_NODE):
		attrset(COLOR_PAIR(9));
		break;
	default:
		break;
	}
	if (max_offset >= offset)
		printw("%s", copy_to);
	max_offset++;
	if(g_list_length(node->irqs) > 0) {
		for_each_irq(node->irqs, display_tree_node_irqs, indent);
	}
	if(g_list_length(node->children)) {
		for_each_node(node->children, display_tree_node, data);
	}
}

void display_tree()
{
	clear();
	char *setup_data = get_data(SETUP);
	parse_setup(setup_data);
	char *irqbalance_data = get_data(STATS);
	parse_into_tree(irqbalance_data);
	display_banned_cpus();
	max_offset = 0;
	for_each_node(tree, display_tree_node, NULL);
	max_offset -= LINES - 5;
	if (max_offset < 0)
		max_offset = 0;
	show_frame();
	show_footer();
	refresh();
	free(setup_data);
	free(irqbalance_data);
}
