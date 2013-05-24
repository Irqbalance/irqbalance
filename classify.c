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
	"virt-event",
	0
};

int map_class_to_level[8] =
{ BALANCE_PACKAGE, BALANCE_CACHE, BALANCE_CACHE, BALANCE_NONE, BALANCE_CORE, BALANCE_CORE, BALANCE_CORE, BALANCE_CORE };


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

struct user_irq_policy {
	int ban;
	int level;
	int numa_node_set;
	int numa_node;
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
		log(TO_CONSOLE, LOG_WARNING, "No memory to ban irq %d\n", irq);
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
static struct irq_info *add_one_irq_to_db(const char *devpath, int irq, struct user_irq_policy *pol)
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
		log(TO_CONSOLE, LOG_INFO, "DROPPING DUPLICATE ENTRY FOR IRQ %d on path %s\n", irq, devpath);
		return NULL;
	}

	entry = g_list_find_custom(banned_irqs, &find, compare_ints);
	if (entry) {
		log(TO_ALL, LOG_INFO, "SKIPPING BANNED IRQ %d\n", irq);
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
	if (pol->level >= 0)
		new->level = pol->level;
	else
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
	if (pol->numa_node_set == 1)
		new->numa_node = get_numa_node(pol->numa_node);
	else
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
	log(TO_CONSOLE, LOG_INFO, "Adding IRQ %d to database\n", irq);
	return new;
}

static void parse_user_policy_key(char *buf, struct user_irq_policy *pol)
{
	char *key, *value, *end;
	char *levelvals[] = { "none", "package", "cache", "core" };
	int idx;

	key = buf;
	value = strchr(buf, '=');

	if (!value) {
		log(TO_SYSLOG, LOG_WARNING, "Bad format for policy, ignoring: %s\n", buf);
		return;
	}

	/* NULL terminate the key and advance value to the start of the value
	 * string
	 */
	*value = '\0';
	value++;
	end = strchr(value, '\n');
	if (end)
		*end = '\0';

	if (!strcasecmp("ban", key)) {
		if (!strcasecmp("false", value))
			pol->ban = 0;
		else if (!strcasecmp("true", value))
			pol->ban = 1;
		else {
			log(TO_ALL, LOG_WARNING, "Unknown value for ban poilcy: %s\n", value);
		}
	} else if (!strcasecmp("balance_level", key)) {
		for (idx=0; idx<4; idx++) {
			if (!strcasecmp(levelvals[idx], value))
				break;
		}

		if (idx>3)
			log(TO_ALL, LOG_WARNING, "Bad value for balance_level policy: %s\n", value);
		else
			pol->level = idx;
	} else if (!strcasecmp("numa_node", key)) {
		idx = strtoul(value, NULL, 10);	
		if (!get_numa_node(idx)) {
			log(TO_ALL, LOG_WARNING, "NUMA node %d doesn't exist\n",
				idx);
			return;
		}
		pol->numa_node = idx;
		pol->numa_node_set = 1;
	} else
		log(TO_ALL, LOG_WARNING, "Unknown key returned, ignoring: %s\n", key);
	
}

/*
 * Calls out to a possibly user defined script to get user assigned poilcy
 * aspects for a given irq.  A value of -1 in a given field indicates no
 * policy was given and that system defaults should be used
 */
static void get_irq_user_policy(char *path, int irq, struct user_irq_policy *pol)
{
	char *cmd;
	FILE *output;
	char buffer[128];
	char *brc;

	memset(pol, -1, sizeof(struct user_irq_policy));

	/* Return defaults if no script was given */
	if (!polscript)
		return;

	cmd = alloca(strlen(path)+strlen(polscript)+64);
	if (!cmd)
		return;

	sprintf(cmd, "exec %s %s %d", polscript, path, irq);
	output = popen(cmd, "r");
	if (!output) {
		log(TO_ALL, LOG_WARNING, "Unable to execute user policy script %s\n", polscript);
		return;
	}

	while(!feof(output)) {
		brc = fgets(buffer, 128, output);
		if (brc)
			parse_user_policy_key(brc, pol);
	}
	pclose(output);
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
	
	sprintf(cmd, "%s %s %d > /dev/null",banscript, path, irq);
	rc = system(cmd);

	/*
 	 * The system command itself failed
 	 */
	if (rc == -1) {
		log(TO_ALL, LOG_WARNING, "%s failed, please check the --banscript option\n", cmd);
		return 0;
	}

	if (WEXITSTATUS(rc)) {
		log(TO_ALL, LOG_INFO, "irq %d is baned by %s\n", irq, banscript);
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
	struct user_irq_policy pol;

	sprintf(path, "%s/%s/msi_irqs", SYSDEV_DIR, dirname);
	
	msidir = opendir(path);

	if (msidir) {
		do {
			entry = readdir(msidir);
			if (!entry)
				break;
			irqnum = strtol(entry->d_name, NULL, 10);
			if (irqnum) {
				get_irq_user_policy(path, irqnum, &pol);
				sprintf(path, "%s/%s", SYSDEV_DIR, dirname);
				if ((pol.ban == 1) || (check_for_irq_ban(path, irqnum))) {
					add_banned_irq(irqnum);
					continue;
				}
				new = add_one_irq_to_db(path, irqnum, &pol);
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
		get_irq_user_policy(path, irqnum, &pol);
		if ((pol.ban == 1) || (check_for_irq_ban(path, irqnum))) {
			add_banned_irq(irqnum);
			goto done;
		}

		new = add_one_irq_to_db(path, irqnum, &pol);
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
			log(TO_CONSOLE, LOG_INFO, "Adding untracked IRQ %d to database\n", ninfo->irq);
			interrupts_db = g_list_append(interrupts_db, ninfo);
		} else
			free(ninfo);

		gentry = g_list_first(new_irq_list);
	}
	g_list_free(new_irq_list);
	new_irq_list = NULL;
		
}

struct irq_info *add_new_irq(int irq, const char *irq_name)
{
	struct irq_info *new, *nnew;

	new = calloc(sizeof(struct irq_info), 1);
	if (!new)
		return NULL;
	nnew = calloc(sizeof(struct irq_info), 1);
	if (!nnew) {
		free(new);
		return NULL;
	}

	new->irq = irq;
	/* Do classification here. As Xen PV is the first resident
	 * here, this is done rather simple.
	 */
	if (strstr(irq_name, "xen-dyn-event") != NULL) {
		new->type = IRQ_TYPE_VIRT_EVENT;
		new->class = IRQ_VIRT_EVENT;
	} else {
		new->type = IRQ_TYPE_LEGACY;
		new->class = IRQ_OTHER;
	}
	new->level = map_class_to_level[new->class];
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

	if (!entry)
		return;

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
