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
static GList *new_irq_list;
static GList *banned_irqs;

#define SYSDEV_DIR "/sys/bus/pci/devices"

static gint compare_ints(gconstpointer a, gconstpointer b)
{
	const struct irq_info *ai = a;
	const struct irq_info *bi = b;

	return ai->irq - bi->irq;
}

void add_banned_irq(int irq)
{
	struct irq_info find, *new;
	GList *entry;

	find.irq = irq;
	entry = g_list_find_custom(banned_irqs, &find, compare_ints);
	if (entry)
		return;

	new = calloc(sizeof(struct irq_info), 1);
	if (!new) {
		if (debug_mode)
			printf("No memory to ban irq %d\n", irq);
		return;
	}

	new->irq = irq;

	banned_irqs = g_list_append(banned_irqs, new);
	return;
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
	char path[PATH_MAX];
	FILE *fd;
	char *lcpu_mask;
	GList *entry;
	ssize_t ret;
	size_t blen;

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

	entry = g_list_find_custom(banned_irqs, &find, compare_ints);
	if (entry) {
		if (debug_mode)
			printf("SKIPPING BANNED IRQ %d\n", irq);
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
	ret = getline(&lcpu_mask, &blen, fd);
	fclose(fd);
	if (ret <= 0) {
		cpus_setall(new->cpumask);
	} else {
		cpumask_parse_user(lcpu_mask, ret, new->cpumask);
	}
	free(lcpu_mask);

assign_affinity_hint:
	cpus_clear(new->affinity_hint);
	sprintf(path, "/proc/irq/%d/affinity_hint", irq);
	fd = fopen(path, "r");
	if (!fd)
		goto out;
	lcpu_mask = NULL;
	ret = getline(&lcpu_mask, &blen, fd);
	fclose(fd);
	if (ret <= 0)
		goto out;
	cpumask_parse_user(lcpu_mask, ret, new->affinity_hint);
	free(lcpu_mask);
out:
	if (debug_mode)
		printf("Adding IRQ %d to database\n", irq);
	return new;
}

static int check_for_irq_ban(char *path, int irq)
{
	char *cmd;
	int rc;

	if (!banscript)
		return 0;

	cmd = alloca(strlen(path)+strlen(banscript)+32);
	if (!cmd)
		return 0;
	
	sprintf(cmd, "%s %s %d",banscript, path, irq);
	rc = system(cmd);

	/*
 	 * The system command itself failed
 	 */
	if (rc == -1) {
		if (debug_mode)
			printf("%s failed, please check the --banscript option\n", cmd);
		else
			syslog(LOG_INFO, "%s failed, please check the --banscript option\n", cmd);
		return 0;
	}

	if (WEXITSTATUS(rc)) {
		if (debug_mode)
			printf("irq %d is baned by %s\n", irq, banscript);
		else
			syslog(LOG_INFO, "irq %d is baned by %s\n", irq, banscript);
		return 1;
	}
	return 0;

}

/*
 * Figures out which interrupt(s) relate to the device we're looking at in dirname
 */
static void build_one_dev_entry(const char *dirname)
{
	struct dirent *entry;
	DIR *msidir;
	FILE *fd;
	int irqnum;
	struct irq_info *new;
	char path[PATH_MAX];

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
				if (check_for_irq_ban(path, irqnum)) {
					add_banned_irq(irqnum);
					continue;
				}
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
		if (check_for_irq_ban(path, irqnum)) {
			add_banned_irq(irqnum);
			goto done;
		}

		new = add_one_irq_to_db(path, irqnum);
		if (!new)
			goto done;
		new->type = IRQ_TYPE_LEGACY;
	}

done:
	fclose(fd);
	return;
}

static void free_irq(struct irq_info *info, void *data __attribute__((unused)))
{
	free(info);
}

void free_irq_db(void)
{
	for_each_irq(NULL, free_irq, NULL);
	g_list_free(interrupts_db);
	interrupts_db = NULL;
}

void rebuild_irq_db(void)
{
	DIR *devdir = opendir(SYSDEV_DIR);
	struct dirent *entry;
	GList *gentry;
	struct irq_info *ninfo, *iinfo;

	free_irq_db();
		
	if (!devdir)
		return;

	do {
		entry = readdir(devdir);

		if (!entry)
			break;

	build_one_dev_entry(entry->d_name);

	} while (entry != NULL);

	closedir(devdir);

	if (!new_irq_list)
		return;
	gentry = g_list_first(new_irq_list);	
	while(gentry) {
		ninfo = gentry->data;
		iinfo = get_irq_info(ninfo->irq);
		new_irq_list = g_list_remove(new_irq_list, ninfo);
		if (!iinfo) {
			if (debug_mode)
				printf("Adding untracked IRQ %d to database\n", ninfo->irq);
			interrupts_db = g_list_append(interrupts_db, ninfo);
		} else
			free(ninfo);

		gentry = g_list_first(new_irq_list);
	}
	g_list_free(new_irq_list);
	new_irq_list = NULL;
		
}

struct irq_info *add_new_irq(int irq)
{
	struct irq_info *new, *nnew;

	new = calloc(sizeof(struct irq_info), 1);
	nnew = calloc(sizeof(struct irq_info), 1);
	if (!new || !nnew)
		return NULL;

	new->irq = irq;
	new->type = IRQ_TYPE_LEGACY;
	new->class = IRQ_OTHER;
	new->numa_node = get_numa_node(-1);
	memcpy(nnew, new, sizeof(struct irq_info));
	interrupts_db = g_list_append(interrupts_db, new);
	new_irq_list = g_list_append(new_irq_list, nnew);
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
	struct irq_info find, *tmp;

	find.irq = info->irq;
	entry = g_list_find_custom(*from, &find, compare_ints);
	tmp = entry->data;
	*from = g_list_delete_link(*from, entry);


	*to = g_list_append(*to, tmp);
	info->moved = 1;
}

static gint sort_irqs(gconstpointer A, gconstpointer B)
{
        struct irq_info *a, *b;
        a = (struct irq_info*)A;
        b = (struct irq_info*)B;

	if (a->class < b->class || a->load < b->load || a < b)
		return 1;
        return -1;
}

void sort_irq_list(GList **list)
{
	*list = g_list_sort(*list, sort_irqs);
}
