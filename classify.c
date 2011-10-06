#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <assert.h>

#include "irqbalance.h"
#include "types.h"


char *classes[] = {
	"other",
	"legacy",
	"storage",
	"timer",
	"ethernet",
	"gbit-ethernet",
	"10gbit-ethernet",
	0
};

int map_class_to_level[7] = 
{ BALANCE_PACKAGE, BALANCE_CACHE, BALANCE_CACHE, BALANCE_NONE, BALANCE_CORE, BALANCE_CORE, BALANCE_CORE };


#define MAX_CLASS 0x12
/*
 * Class codes lifted from pci spec, appendix D.
 * and mapped to irqbalance types here
 */
static short class_codes[MAX_CLASS] = {
	IRQ_OTHER,
	IRQ_SCSI,
	IRQ_ETH,
	IRQ_OTHER,
	IRQ_OTHER,
	IRQ_OTHER,
	IRQ_LEGACY,
	IRQ_OTHER,
	IRQ_OTHER,
	IRQ_LEGACY,
	IRQ_OTHER,
	IRQ_OTHER,
	IRQ_LEGACY,
	IRQ_ETH,
	IRQ_SCSI,
	IRQ_OTHER,
	IRQ_OTHER,
	IRQ_OTHER,
};

static GList *interrupts_db;

#define SYSDEV_DIR "/sys/bus/pci/devices"

static gint compare_ints(gconstpointer a, gconstpointer b)
{
	const struct irq_info *ai = a;
	const struct irq_info *bi = b;

	return ai->irq - bi->irq;
}

static void free_int(gpointer data)
{
	free(data);
}

/*
 * Inserts an irq_info struct into the intterupts_db list
 * devpath points to the device directory in sysfs for the 
 * related device
 */
static struct irq_info *add_one_irq_to_db(const char *devpath, int irq)
{
	int class = 0;
	int rc;
	struct irq_info *new, find;
	int numa_node;
	char *path = alloca(strlen(devpath) + 64);
	FILE *fd;
	char *lcpu_mask;
	GList *entry;

	/*
	 * First check to make sure this isn't a duplicate entry
	 */
	find.irq = irq;
	entry = g_list_find_custom(interrupts_db, &find, compare_ints);
	if (entry) {
		if (debug_mode)
			printf("DROPPING DUPLICATE ENTRY FOR IRQ %d on path %s\n", irq, devpath);
		return NULL;
	}

	new = calloc(sizeof(struct irq_info), 1);
	if (!new)
		return NULL;

	new->irq = irq;
	new->class = IRQ_OTHER;

	interrupts_db = g_list_append(interrupts_db, new);

	sprintf(path, "%s/class", devpath);

	fd = fopen(path, "r");

	if (!fd) {
		perror("Can't open class file: ");
		goto get_numa_node;
	}

	rc = fscanf(fd, "%x", &class);
	fclose(fd);

	if (!rc)
		goto get_numa_node;

	/*
	 * Restrict search to major class code
	 */
	class >>= 16;

	if (class >= MAX_CLASS)
		goto get_numa_node;

	new->class = class_codes[class];
	new->level = map_class_to_level[class_codes[class]];

get_numa_node:
	numa_node = -1;
	sprintf(path, "%s/numa_node", devpath);
	fd = fopen(path, "r");
	if (!fd)
		goto assign_node;

	rc = fscanf(fd, "%d", &numa_node);
	fclose(fd);

assign_node:
	new->numa_node = get_numa_node(numa_node);

	sprintf(path, "%s/local_cpus", devpath);
	fd = fopen(path, "r");
	if (!fd) {
		cpus_setall(new->cpumask);
		goto assign_affinity_hint;
	}
	lcpu_mask = NULL;
	rc = fscanf(fd, "%as", &lcpu_mask);
	fclose(fd);
	if (!lcpu_mask || !rc) {
		cpus_setall(new->cpumask);
	} else {
		cpumask_parse_user(lcpu_mask, strlen(lcpu_mask),
				   new->cpumask);
	}
	free(lcpu_mask);

assign_affinity_hint:
	cpus_clear(new->affinity_hint);
	sprintf(path, "/proc/irq/%d/affinity_hint", irq);
	fd = fopen(path, "r");
	if (!fd)
		goto out;
	lcpu_mask = NULL;
	rc = fscanf(fd, "%as", &lcpu_mask);
	fclose(fd);
	if (!lcpu_mask)
		goto out;
	cpumask_parse_user(lcpu_mask, strlen(lcpu_mask),
			   new->affinity_hint);
	free(lcpu_mask);
out:
	if (debug_mode)
		printf("Adding IRQ %d to database\n", irq);
	return new;
}

/*
 * Figures out which interrupt(s) relate to the device we're looking at in dirname
 */
static void build_one_dev_entry(const char *dirname)
{
	size_t pathlen = strlen(SYSDEV_DIR) + strlen(dirname) + 128;
	struct dirent *entry;
	DIR *msidir;
	FILE *fd;
	int irqnum;
	struct irq_info *new;
	char *path = alloca(pathlen);

	if (!path)
		return;

	sprintf(path, "%s/%s/msi_irqs", SYSDEV_DIR, dirname);
	
	msidir = opendir(path);

	if (msidir) {
		do {
			entry = readdir(msidir);
			if (!entry)
				break;
			irqnum = strtol(entry->d_name, NULL, 10);
			if (irqnum) {
				sprintf(path, "%s/%s", SYSDEV_DIR, dirname);
				new = add_one_irq_to_db(path, irqnum);
				if (!new)
					continue;
				new->type = IRQ_TYPE_MSIX;
			}
		} while (entry != NULL);
		closedir(msidir);
		return;
	}

	sprintf(path, "%s/%s/irq", SYSDEV_DIR, dirname);
	fd = fopen(path, "r");
	if (!fd)
		return;
	if (fscanf(fd, "%d", &irqnum) < 0)
		goto done;

	/*
	 * no pci device has irq 0
	 */
	if (irqnum) {
		sprintf(path, "%s/%s", SYSDEV_DIR, dirname);
		new = add_one_irq_to_db(path, irqnum);
		if (!new)
			goto done;
		new->type = IRQ_TYPE_LEGACY;
	}

done:
	fclose(fd);
	return;
}

void free_irq_db(void)
{
	g_list_free_full(interrupts_db, free_int);
}

void rebuild_irq_db(void)
{
	DIR *devdir = opendir(SYSDEV_DIR);
	struct dirent *entry;

	g_list_free_full(interrupts_db, free_int);
		
	if (!devdir)
		return;

	do {
		entry = readdir(devdir);

		if (!entry)
			break;

	build_one_dev_entry(entry->d_name);

	} while (entry != NULL);
	closedir(devdir);
}

struct irq_info *add_misc_irq(int irq)
{
	struct irq_info *new;

	new = calloc(sizeof(struct irq_info), 1);
	if (!new)
		return NULL;

	new->irq = irq;
	new->type = IRQ_TYPE_LEGACY;
	new->class = IRQ_OTHER;
	new->numa_node = get_numa_node(0);
	interrupts_db = g_list_append(interrupts_db, new);
	return new;
}

void for_each_irq(GList *list, void (*cb)(struct irq_info *info, void *data), void *data)
{
	GList *entry = g_list_first(list ? list : interrupts_db);
	GList *next;

	while (entry) {
		next = g_list_next(entry);
		cb(entry->data, data);
		entry = next;
	}
}

struct irq_info *get_irq_info(int irq)
{
	GList *entry;
	struct irq_info find;

	find.irq = irq;
	entry = g_list_find_custom(interrupts_db, &find, compare_ints);
	return entry ? entry->data : NULL;
}

void migrate_irq(GList **from, GList **to, struct irq_info *info)
{
	GList *entry;
	struct irq_info find, *tmp;;

	if (from != NULL) {
		find.irq = info->irq;
		entry = g_list_find_custom(*from, &find, compare_ints);
		tmp = entry->data;
		*from = g_list_delete_link(*from, entry);
	} else
		tmp = info;


	*to = g_list_append(*to, tmp);
	info->moved = 1;
}

static gint sort_irqs(gconstpointer A, gconstpointer B)
{
        struct irq_info *a, *b;
        a = (struct irq_info*)A;
        b = (struct irq_info*)B;

	if (a->class < b->class)
		return 1;
	if (a->class > b->class)
		return -1;
	if (a->load < b->load)
		return 1;
	if (a->load > b->load)
		return -1;
	if (a<b)
		return 1;
        return -1;

}

void sort_irq_list(GList **list)
{
	*list = g_list_sort(*list, sort_irqs);
}
