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
	"video",
	"ethernet",
	"gbit-ethernet",
	"10gbit-ethernet",
	"virt-event",
	0
};

static int map_class_to_level[8] =
{ BALANCE_PACKAGE, BALANCE_CACHE, BALANCE_CORE, BALANCE_CORE, BALANCE_CORE, BALANCE_CORE, BALANCE_CORE, BALANCE_CORE };

struct user_irq_policy {
	int ban;
	int level;
	int numa_node_set;
	int numa_node;
	enum hp_e hintpolicy;
};

static GList *interrupts_db = NULL;
static GList *banned_irqs = NULL;
static GList *cl_banned_irqs = NULL;

#define SYSDEV_DIR "/sys/bus/pci/devices"

#define PCI_MAX_CLASS 0x14
#define PCI_MAX_SERIAL_SUBCLASS 0x81

static int get_pci_irq_class(int pci_class)
{
	int major = pci_class >> 16;
	int sub = (pci_class & 0xFF00) >> 8;
	short irq_class = IRQ_NODEF;
	/*
	 * Class codes lifted from below PCI-SIG spec:
	 *
	 * PCI Code and ID Assignment Specification v1.5
	 *
	 * and mapped to irqbalance types here.
	 *
	 * IRQ_NODEF will go through classification by PCI sub-class code.
	 */
	static short major_class_codes[PCI_MAX_CLASS] = {
		IRQ_OTHER,
		IRQ_SCSI,
		IRQ_ETH,
		IRQ_VIDEO,
		IRQ_OTHER,
		IRQ_OTHER,
		IRQ_LEGACY,
		IRQ_OTHER,
		IRQ_OTHER,
		IRQ_LEGACY,
		IRQ_OTHER,
		IRQ_OTHER,
		IRQ_NODEF,
		IRQ_ETH,
		IRQ_SCSI,
		IRQ_OTHER,
		IRQ_OTHER,
		IRQ_OTHER,
		IRQ_LEGACY,
		IRQ_LEGACY,
	};

	/*
	 * All sub-class code for serial bus controllers.
	 * The major class code is 0xc.
	 */
	static short serial_sub_codes[PCI_MAX_SERIAL_SUBCLASS] = {
		IRQ_LEGACY,
		IRQ_LEGACY,
		IRQ_LEGACY,
		IRQ_LEGACY,
		IRQ_SCSI,
		IRQ_LEGACY,
		IRQ_SCSI,
		IRQ_LEGACY,
		IRQ_LEGACY,
		IRQ_LEGACY,
		[0xa ... 0x7f] = IRQ_NODEF,
		IRQ_LEGACY,
	};

	/*
	 * Check major class code first
	 */

	if (major >= PCI_MAX_CLASS)
		return IRQ_NODEF;

	switch (major) {
		case 0xc: /* Serial bus class */
			if (sub >= PCI_MAX_SERIAL_SUBCLASS)
				return IRQ_NODEF;
			irq_class = serial_sub_codes[sub];
			break;
		default: /* All other PCI classes */
			irq_class = major_class_codes[major];
			break;
	}

	return irq_class;
}

static gint compare_ints(gconstpointer a, gconstpointer b)
{
	const struct irq_info *ai = a;
	const struct irq_info *bi = b;

	return ai->irq - bi->irq;
}

static void add_banned_irq(int irq, GList **list)
{
	struct irq_info find, *new;
	GList *entry;

	find.irq = irq;
	entry = g_list_find_custom(*list, &find, compare_ints);
	if (entry)
		return;

	new = calloc(sizeof(struct irq_info), 1);
	if (!new) {
		log(TO_CONSOLE, LOG_WARNING, "No memory to ban irq %d\n", irq);
		return;
	}

	new->irq = irq;
	new->flags |= IRQ_FLAG_BANNED;
	new->hint_policy = HINT_POLICY_EXACT;

	*list = g_list_append(*list, new);
	return;
}

void add_cl_banned_irq(int irq)
{
	add_banned_irq(irq, &cl_banned_irqs);
}


static int is_banned_irq(int irq)
{
	GList *entry;
	struct irq_info find;

	find.irq = irq;

	entry = g_list_find_custom(banned_irqs, &find, compare_ints);
	return entry ? 1:0;
}

			
/*
 * Inserts an irq_info struct into the intterupts_db list
 * devpath points to the device directory in sysfs for the 
 * related device
 */
static struct irq_info *add_one_irq_to_db(const char *devpath, int irq, struct user_irq_policy *pol)
{
	int pci_class = 0;
	int irq_class = IRQ_OTHER;
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

	if (is_banned_irq(irq)) {
		log(TO_ALL, LOG_INFO, "SKIPPING BANNED IRQ %d\n", irq);
		return NULL;
	}

	new = calloc(sizeof(struct irq_info), 1);
	if (!new)
		return NULL;

	new->irq = irq;
	new->class = IRQ_OTHER;
	new->hint_policy = pol->hintpolicy; 

	interrupts_db = g_list_append(interrupts_db, new);

	sprintf(path, "%s/class", devpath);

	fd = fopen(path, "r");

	if (!fd) {
		perror("Can't open class file: ");
		goto get_numa_node;
	}

	rc = fscanf(fd, "%x", &pci_class);
	fclose(fd);

	if (!rc)
		goto get_numa_node;


	/*
	 * Map PCI class code to irq class
	 */
	irq_class = get_pci_irq_class(pci_class);

	if (irq_class < 0) {
		log(TO_CONSOLE, LOG_WARNING, "Invalid PCI class code %d\n", pci_class);
		goto get_numa_node;
	}

	new->class = irq_class;
	if (pol->level >= 0)
		new->level = pol->level;
	else
		new->level = map_class_to_level[irq_class];

get_numa_node:
	numa_node = -1;
	if (numa_avail) {
		sprintf(path, "%s/numa_node", devpath);
		fd = fopen(path, "r");
		if (fd) {
			rc = fscanf(fd, "%d", &numa_node);
			fclose(fd);
		}
	}

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

static void parse_user_policy_key(char *buf, int irq, struct user_irq_policy *pol)
{
	char *key, *value, *end;
	char *levelvals[] = { "none", "package", "cache", "core" };
	int idx;
	int key_set = 1;

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
			key_set = 0;
			log(TO_ALL, LOG_WARNING, "Unknown value for ban poilcy: %s\n", value);
		}
	} else if (!strcasecmp("balance_level", key)) {
		for (idx=0; idx<4; idx++) {
			if (!strcasecmp(levelvals[idx], value))
				break;
		}

		if (idx>3) {
			key_set = 0;
			log(TO_ALL, LOG_WARNING, "Bad value for balance_level policy: %s\n", value);
		} else
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
	} else if (!strcasecmp("hintpolicy", key)) {
		if (!strcasecmp("exact", value))
			pol->hintpolicy = HINT_POLICY_EXACT;
		else if (!strcasecmp("subset", value))
			pol->hintpolicy = HINT_POLICY_SUBSET;
		else if (!strcasecmp("ignore", value))
			pol->hintpolicy = HINT_POLICY_IGNORE;
		else {
			key_set = 0;
			log(TO_ALL, LOG_WARNING, "Unknown value for hitpolicy: %s\n", value);
		}
	} else {
		key_set = 0;
		log(TO_ALL, LOG_WARNING, "Unknown key returned, ignoring: %s\n", key);
	}

	if (key_set)
		log(TO_ALL, LOG_INFO, "IRQ %d: Override %s to %s\n", irq, key, value);

	
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
	pol->hintpolicy = global_hint_policy;

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
			parse_user_policy_key(brc, irq, pol);
	}
	pclose(output);
}

static int check_for_irq_ban(char *path, int irq)
{
	char *cmd;
	int rc;
	struct irq_info find;
	GList *entry;

	/*
	 * Check to see if we banned this irq on the command line
	 */
	find.irq = irq;
	entry = g_list_find_custom(cl_banned_irqs, &find, compare_ints);
	if (entry)
		return 1;

	if (!banscript)
		return 0;

	if (!path)
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
	char devpath[PATH_MAX];
	struct user_irq_policy pol;

	sprintf(path, "%s/%s/msi_irqs", SYSDEV_DIR, dirname);
	sprintf(devpath, "%s/%s", SYSDEV_DIR, dirname);
	
	msidir = opendir(path);

	if (msidir) {
		do {
			entry = readdir(msidir);
			if (!entry)
				break;
			irqnum = strtol(entry->d_name, NULL, 10);
			if (irqnum) {
				new = get_irq_info(irqnum);
				if (new)
					continue;
				get_irq_user_policy(devpath, irqnum, &pol);
				if ((pol.ban == 1) || (check_for_irq_ban(devpath, irqnum))) {
					add_banned_irq(irqnum, &banned_irqs);
					continue;
				}
				new = add_one_irq_to_db(devpath, irqnum, &pol);
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
		new = get_irq_info(irqnum);
		if (new)
			goto done;
		get_irq_user_policy(devpath, irqnum, &pol);
		if ((pol.ban == 1) || (check_for_irq_ban(path, irqnum))) {
			add_banned_irq(irqnum, &banned_irqs);
			goto done;
		}

		new = add_one_irq_to_db(devpath, irqnum, &pol);
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
	for_each_irq(banned_irqs, free_irq, NULL);
	g_list_free(banned_irqs);
	banned_irqs = NULL;
	g_list_free(rebalance_irq_list);
	rebalance_irq_list = NULL;
}

static void add_new_irq(int irq, struct irq_info *hint)
{
	struct irq_info *new;
	struct user_irq_policy pol;

	new = get_irq_info(irq);
	if (new)
		return;

	get_irq_user_policy("/sys", irq, &pol);
	if ((pol.ban == 1) || check_for_irq_ban(NULL, irq)) {
		add_banned_irq(irq, &banned_irqs);
		new = get_irq_info(irq);
	} else
		new = add_one_irq_to_db("/sys", irq, &pol);

	if (!new) {
		log(TO_CONSOLE, LOG_WARNING, "add_new_irq: Failed to add irq %d\n", irq);
		return;
	}

	/*
	 * Override some of the new irq defaults here
	 */
	if (hint) {
		new->type = hint->type;
		new->class = hint->class;
	}

	new->level = map_class_to_level[new->class];
}

static void add_missing_irq(struct irq_info *info, void *unused __attribute__((unused)))
{
	struct irq_info *lookup = get_irq_info(info->irq);

	if (!lookup)
		add_new_irq(info->irq, info);
	
}


void rebuild_irq_db(void)
{
	DIR *devdir;
	struct dirent *entry;
	GList *tmp_irqs = NULL;

	free_irq_db();
		
	tmp_irqs = collect_full_irq_list();

	devdir = opendir(SYSDEV_DIR);
	if (!devdir)
		goto free;

	do {
		entry = readdir(devdir);

		if (!entry)
			break;

		build_one_dev_entry(entry->d_name);

	} while (entry != NULL);

	closedir(devdir);


	for_each_irq(tmp_irqs, add_missing_irq, NULL);

free:
	g_list_free_full(tmp_irqs, free);

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

	if (!entry)
		entry = g_list_find_custom(banned_irqs, &find, compare_ints);

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
