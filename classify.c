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


int class_counts[7];

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
union property {
	int int_val;
	cpumask_t mask_val;
};

enum irq_type {
	INT_TYPE = 0,
	CPUMASK_TYPE,
};

struct irq_property {
	enum irq_type itype;
	union property iproperty;
};
#define iint_val iproperty.int_val
#define imask_val iproperty.mask_val

struct irq_info {
	int irq;
	struct irq_property property[IRQ_MAX_PROPERTY];
};

static void init_new_irq(struct irq_info *new)
{
	new->property[IRQ_CLASS].itype = INT_TYPE;
	new->property[IRQ_TYPE].itype = INT_TYPE;
	new->property[IRQ_NUMA].itype = INT_TYPE;
	new->property[IRQ_LCPU_MASK].itype = CPUMASK_TYPE;
}

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

	new = malloc(sizeof(struct irq_info));
	if (!new)
		return NULL;
	init_new_irq(new);

	new->irq = irq;
	new->property[IRQ_CLASS].iint_val = IRQ_OTHER;

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

	new->property[IRQ_CLASS].iint_val = class_codes[class];
get_numa_node:
	numa_node = -1;
	sprintf(path, "%s/numa_node", devpath);
	fd = fopen(path, "r");
	if (!fd)
		goto assign_node;

	rc = fscanf(fd, "%d", &numa_node);
	fclose(fd);

assign_node:
	new->property[IRQ_NUMA].iint_val = numa_node;

	sprintf(path, "%s/local_cpus", devpath);
	fd = fopen(path, "r");
	if (!fd) {
		cpus_setall(new->property[IRQ_LCPU_MASK].imask_val);
		goto out;
	}
	lcpu_mask = NULL;
	rc = fscanf(fd, "%as", &lcpu_mask);
	fclose(fd);
	if (!lcpu_mask) {
		cpus_setall(new->property[IRQ_LCPU_MASK].imask_val);
	} else {
		cpumask_parse_user(lcpu_mask, strlen(lcpu_mask),
				   new->property[IRQ_LCPU_MASK].imask_val);
	}
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
				new->property[IRQ_TYPE].iint_val = IRQ_TYPE_MSIX;
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
		new->property[IRQ_TYPE].iint_val = IRQ_TYPE_LEGACY;
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

static GList *add_misc_irq(int irq)
{
	struct irq_info *new, find;

	new = malloc(sizeof(struct irq_info));
	if (!new)
		return NULL;
	init_new_irq(new);

	new->irq = irq;
	new->property[IRQ_TYPE].iint_val = IRQ_TYPE_LEGACY;
	new->property[IRQ_CLASS].iint_val = IRQ_OTHER;
	new->property[IRQ_NUMA].iint_val = -1;
	interrupts_db = g_list_append(interrupts_db, new);
	find.irq = irq;
	return g_list_find_custom(interrupts_db, &find, compare_ints);	
}

int find_irq_integer_prop(int irq, enum irq_prop prop)
{
	GList *entry;
	struct irq_info find, *result;
	
	find.irq = irq;

	entry = g_list_find_custom(interrupts_db, &find, compare_ints);

	if (!entry) {
		if (debug_mode)
			printf("No entry for irq %d in the irq database, adding default entry\n", irq);
		entry = add_misc_irq(irq);
	}

	result = entry->data;
	assert(result->property[prop].itype == INT_TYPE);
	return result->property[prop].iint_val;
}

cpumask_t find_irq_cpumask_prop(int irq, enum irq_prop prop)
{
	GList *entry;
	struct irq_info find, *result;
	
	find.irq = irq;

	entry = g_list_find_custom(interrupts_db, &find, compare_ints);

	if (!entry) {
		if (debug_mode)
			printf("No entry for irq %d in the irq database, adding default entry\n", irq);
		entry = add_misc_irq(irq);
	}

	result = entry->data;
	assert(result->property[prop].itype == CPUMASK_TYPE);
	return result->property[prop].imask_val;
}

int get_next_irq(int irq)
{
	GList *entry;
	struct irq_info *irqp, find;

	if (irq == -1) {
		entry = g_list_first(interrupts_db);
		irqp = entry->data;
		return irqp->irq;
	}

	find.irq = irq;
	entry = g_list_find_custom(interrupts_db, &find, compare_ints);
	if (!entry)
		return -1;

	entry = g_list_next(entry);
	if (!entry)
		return -1;
	irqp= entry->data;
	return irqp->irq;
}
