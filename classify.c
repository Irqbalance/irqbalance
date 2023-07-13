#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <assert.h>
#include <errno.h>
#include <libgen.h>

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
};

static GList *interrupts_db = NULL;
static GList *banned_irqs = NULL;
GList *cl_banned_irqs = NULL;
static GList *cl_banned_modules = NULL;

#define SYSFS_DIR "/sys"
#define SYSPCI_DIR "/sys/bus/pci/devices"

#define PCI_MAX_CLASS 0x14
#define PCI_MAX_SERIAL_SUBCLASS 0x81

#define PCI_INVAL_DATA 0xFFFFFFFF

struct pci_info {
	unsigned short vendor;
	unsigned short device;
	unsigned short sub_vendor;
	unsigned short sub_device;
	unsigned int class;
};

/* PCI vendor ID, device ID */
#define PCI_VENDOR_PLX 0x10b5
#define PCI_DEVICE_PLX_PEX8619 0x8619
#define PCI_VENDOR_CAVIUM 0x177d
#define PCI_DEVICE_CAVIUM_CN61XX 0x0093

/* PCI subsystem vendor ID, subsystem device ID */
#define PCI_SUB_VENDOR_EMC 0x1120
#define PCI_SUB_DEVICE_EMC_055B 0x055b
#define PCI_SUB_DEVICE_EMC_0568 0x0568
#define PCI_SUB_DEVICE_EMC_dd00 0xdd00

/*
 * Apply software workarounds for some special devices
 *
 * The world is not perfect and supplies us with broken PCI devices.
 * Usually there are two sort of cases:
 *
 *     1. The device is special
 *        Before shipping the devices, PCI spec doesn't have the definitions.
 *
 *     2. Buggy PCI devices
 *        Some PCI devices don't follow the PCI class code definitions.
 */
static void apply_pci_quirks(const struct pci_info *pci, int *irq_class)
{
	if ((pci->vendor == PCI_VENDOR_PLX) &&
	    (pci->device == PCI_DEVICE_PLX_PEX8619) &&
	    (pci->sub_vendor == PCI_SUB_VENDOR_EMC)) {
		switch (pci->sub_device) {
			case PCI_SUB_DEVICE_EMC_055B:
			case PCI_SUB_DEVICE_EMC_dd00:
				*irq_class = IRQ_SCSI;
				break;
		}
	}

	if ((pci->vendor == PCI_VENDOR_CAVIUM) &&
	    (pci->device == PCI_DEVICE_CAVIUM_CN61XX) &&
	    (pci->sub_vendor == PCI_SUB_VENDOR_EMC)) {
		switch (pci->sub_device) {
			case PCI_SUB_DEVICE_EMC_0568:
				*irq_class = IRQ_SCSI;
				break;
		}
	}

	return;
}

/* Determin IRQ class based on PCI class code */
static int map_pci_irq_class(unsigned int pci_class)
{
	unsigned int major = pci_class >> 16;
	unsigned int sub = (pci_class & 0xFF00) >> 8;
	int irq_class = IRQ_NODEF;
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

/* Read specific data from sysfs */
static unsigned int read_pci_data(const char *devpath, const char* file)
{
	char path[PATH_MAX];
	unsigned int data = PCI_INVAL_DATA;

	sprintf(path, "%s/%s", devpath, file);
	if (process_one_line(path, get_hex, &data) < 0)
		log(TO_CONSOLE, LOG_WARNING, "PCI: can't get from file:%s\n", path);

	return data;
}

/* Get pci information for IRQ classification */
static int get_pci_info(const char *devpath, struct pci_info *pci)
{
	unsigned int data = PCI_INVAL_DATA;

	if ((data = read_pci_data(devpath, "vendor")) == PCI_INVAL_DATA)
		return -ENODEV;
	pci->vendor = (unsigned short)data;

	if ((data = read_pci_data(devpath, "device")) == PCI_INVAL_DATA)
		return -ENODEV;
	pci->device = (unsigned short)data;

	if ((data = read_pci_data(devpath, "subsystem_vendor")) == PCI_INVAL_DATA)
		return -ENODEV;
	pci->sub_vendor = (unsigned short)data;

	if ((data = read_pci_data(devpath, "subsystem_device")) == PCI_INVAL_DATA)
		return -ENODEV;
	pci->sub_device = (unsigned short)data;

	if ((data = read_pci_data(devpath, "class")) == PCI_INVAL_DATA)
		return -ENODEV;
	pci->class = data;

	return 0;
}

/* Return IRQ class for given devpath */
static int get_irq_class(const char *devpath)
{
	int irq_class = IRQ_NODEF;
	struct pci_info pci;

	/* Get PCI info from sysfs */
	if (get_pci_info(devpath, &pci) < 0)
		return IRQ_NODEF;

	/* Map PCI class code to irq class */
	irq_class = map_pci_irq_class(pci.class);
	if (irq_class < 0) {
		log(TO_CONSOLE, LOG_WARNING, "Invalid PCI class code %d\n",
		    pci.class);
		return IRQ_NODEF;
	}

	/* Reassign irq class for some buggy devices */
	apply_pci_quirks(&pci, &irq_class);

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

	new = calloc(1, sizeof(struct irq_info));
	if (!new) {
		log(TO_CONSOLE, LOG_WARNING, "No memory to ban irq %d\n", irq);
		return;
	}

	new->irq = irq;
	new->flags |= IRQ_FLAG_BANNED;

	*list = g_list_append(*list, new);
	log(TO_CONSOLE, LOG_INFO, "IRQ %d was BANNED.\n", irq);
	return;
}

void add_cl_banned_irq(int irq)
{
	add_banned_irq(irq, &cl_banned_irqs);
}

gint substr_find(gconstpointer a, gconstpointer b)
{
	if (strstr(b, a))
		return 0;
	else
		return 1;
}

static void add_banned_module(char *modname, GList **modlist)
{
	GList *entry;
	char *newmod;
	
	entry = g_list_find_custom(*modlist, modname, substr_find);
	if (entry)
		return;

	newmod = strdup(modname);
	if (!newmod) {
		log(TO_CONSOLE, LOG_WARNING, "No memory to ban module %s\n", modname);
		return;
	}

	*modlist = g_list_append(*modlist, newmod);
}

void add_cl_banned_module(char *modname)
{
	add_banned_module(modname, &cl_banned_modules);
}

			
/*
 * Inserts an irq_info struct into the intterupts_db list
 * devpath points to the device directory in sysfs for the 
 * related device. NULL devpath means no sysfs entries for
 * this irq.
 */
static struct irq_info *add_one_irq_to_db(const char *devpath, struct irq_info *hint, struct user_irq_policy *pol)
{
	int irq = hint->irq;
	struct irq_info *new;
	int numa_node;
	char path[PATH_MAX];

	new = calloc(1, sizeof(struct irq_info));
	if (!new)
		return NULL;

	new->irq = irq;
	new->type = hint->type;
	new->class = hint->class;

	interrupts_db = g_list_append(interrupts_db, new);

 	/* Some special irqs have NULL devpath */
	if (devpath != NULL) {
		/* Map PCI class code to irq class */
		int irq_class = get_irq_class(devpath);
		if (irq_class < 0)
			goto get_numa_node;
		new->class = irq_class;
	}

	if (pol->level >= 0)
		new->level = pol->level;
	else
		new->level = map_class_to_level[new->class];

get_numa_node:
	numa_node = NUMA_NO_NODE;
	if (numa_avail) {
		if (devpath != NULL) {
			sprintf(path, "%s/numa_node", devpath);
			process_one_line(path, get_int, &numa_node);
		} else {
			sprintf(path, "/proc/irq/%i/node", irq);
			process_one_line(path, get_int, &numa_node);
		}
	}

	if (pol->numa_node_set == 1)
		new->numa_node = get_numa_node(pol->numa_node);
	else
		new->numa_node = get_numa_node(numa_node);

	if (!new->numa_node) {
		log(TO_CONSOLE, LOG_WARNING, "IRQ %d has an unknown node\n", irq);
		new->numa_node = get_numa_node(NUMA_NO_NODE);
	}

	cpus_setall(new->cpumask);
	if (devpath != NULL) {
		sprintf(path, "%s/local_cpus", devpath);
		process_one_line(path, get_mask_from_bitmap, &new->cpumask);
	}

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
			log(TO_ALL, LOG_WARNING, "Unknown value for ban policy: %s\n", value);
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
	} else {
		key_set = 0;
		log(TO_ALL, LOG_WARNING, "Unknown key returned, ignoring: %s\n", key);
	}

	if (key_set)
		log(TO_ALL, LOG_INFO, "IRQ %d: Override %s to %s\n", irq, key, value);

	
}

static int run_script_for_policy(char *script, char *path, int irq, struct user_irq_policy *pol)
{
	char *cmd;
	char *brc;
	FILE *output;
	char buffer[128];

	cmd = alloca(strlen(path)+strlen(script)+64);
	if (!cmd)
		return -1;

	sprintf(cmd, "exec %s %s %d", script, path, irq);
	output = popen(cmd, "r");
	if (!output) {
		log(TO_ALL, LOG_WARNING, "Unable to execute user policy script %s\n", script);
		return 1; /* tell caller to ignore this script */
	}

	while(!feof(output)) {
		brc = fgets(buffer, 128, output);
		if (brc)
			parse_user_policy_key(brc, irq, pol);
	}
	return WEXITSTATUS(pclose(output));
}

/*
 * Calls out to a possibly user defined script to get user assigned policy
 * aspects for a given irq.  A value of -1 in a given field indicates no
 * policy was given and that system defaults should be used
 */
static void get_irq_user_policy(char *path, int irq, struct user_irq_policy *pol)
{
	struct stat sbuf;
	DIR *poldir;
	struct dirent *entry;
	int ret;
	char script[1024];

	memset(pol, -1, sizeof(struct user_irq_policy));

	/* Return defaults if no script was given */
	if (!polscript)
		return;

	if (stat(polscript, &sbuf))
		return;

	/* Use SYSFS_DIR for irq has no sysfs entries */
	if (!path)
		path = SYSFS_DIR;

	if (!S_ISDIR(sbuf.st_mode)) {
		if (run_script_for_policy(polscript, path, irq, pol) != 0) {
			log(TO_CONSOLE, LOG_ERR, "policy script returned non-zero code!  skipping user policy\n");
			memset(pol, -1, sizeof(struct user_irq_policy));
		}
	} else {
		/* polscript is a directory, user multiple script semantics */
		poldir = opendir(polscript);

		if (poldir) {
			while ((entry = readdir(poldir)) != NULL) {
				snprintf(script, sizeof(script), "%s/%s", polscript, entry->d_name);
				if (stat(script, &sbuf))
					continue;
				if (S_ISREG(sbuf.st_mode)) {
					if (!(sbuf.st_mode & S_IXUSR)) {
						log(TO_CONSOLE, LOG_DEBUG, "Skipping script %s due to lack of executable permission\n", script);
						continue;
					}

					memset(pol, -1, sizeof(struct user_irq_policy));
					ret = run_script_for_policy(script, path, irq, pol);
					if ((ret < 0) || (ret >= 2)) {
						log(TO_CONSOLE, LOG_ERR, "Error executing policy script %s : %d\n", script, ret);
						continue;
					}

					/* a ret of 1 means this script isn't
 					 * for this irq
 					 */
					if (ret == 1)
						continue;

					log(TO_CONSOLE, LOG_DEBUG, "Accepting script %s to define policy for irq %d\n", script, irq);
					break;
				}
			}
			closedir(poldir);
		}
	}
}

static int check_for_module_ban(char *name)
{
	GList *entry;

	entry = g_list_find_custom(cl_banned_modules, name, substr_find);

	if (entry)
		return 1;
	else
		return 0;
}

static int check_for_irq_ban(struct irq_info *irq, char *mod)
{
	GList *entry;

	/*
	 * Check to see if we banned this irq on the command line
	 */
	entry = g_list_find_custom(cl_banned_irqs, irq, compare_ints);
	if (entry)
		return 1;

	/*
	 * Check to see if we banned module which the irq belongs to.
	 */
	if (mod != NULL && strlen(mod) > 0 && check_for_module_ban(mod))
		return 1;

    /*
     * Check if any banned modules are substrings in irq->name
     */
	if (irq->name != NULL && strlen(irq->name) > 0 && check_for_module_ban(irq->name))
		return 1;

	return 0;
}

static void add_new_irq(char *path, struct irq_info *hint)
{
	struct irq_info *new;
	struct user_irq_policy pol;
	int irq = hint->irq;
	char buf[PATH_MAX], drvpath[PATH_MAX];
	char *mod = NULL;
	int ret;

	new = get_irq_info(irq);
	if (new)
		return;

	if (path) {
		sprintf(buf, "%s/driver", path);
		ret = readlink(buf, drvpath, PATH_MAX);
		if (ret > 0 && ret < PATH_MAX) {
			drvpath[ret] = '\0';
			mod = basename(drvpath);
		}
	}
	/* Set NULL devpath for the irq has no sysfs entries */
	get_irq_user_policy(path, irq, &pol);
	if ((pol.ban == 1) || check_for_irq_ban(hint, mod)) { /*FIXME*/
		add_banned_irq(irq, &banned_irqs);
		new = get_irq_info(irq);
	} else
		new = add_one_irq_to_db(path, hint, &pol);

	if (!new)
		log(TO_CONSOLE, LOG_WARNING, "add_new_irq: Failed to add irq %d\n", irq);
}

/*
 * Figures out which interrupt(s) relate to the device we"re looking at in dirname
 */
static void build_one_dev_entry(const char *dirname, int build_irq)
{
	struct dirent *entry;
	DIR *msidir;
	int irqnum;
	struct irq_info hint = {0};
	char path[PATH_MAX];
	char devpath[PATH_MAX];

	sprintf(path, "%s/%s/msi_irqs", SYSPCI_DIR, dirname);
	sprintf(devpath, "%s/%s", SYSPCI_DIR, dirname);

	/* Needs to be further classified */
	hint.class = IRQ_OTHER;
	
	msidir = opendir(path);

	if (msidir) {
		do {
			entry = readdir(msidir);
			if (!entry)
				break;
			irqnum = strtol(entry->d_name, NULL, 10);
			/* If build_irq is valid, only add irq when it's number equals to  build_irq */
			if (irqnum && ((build_irq < 0) || (irqnum == build_irq))) {
				hint.irq = irqnum;
				hint.type = IRQ_TYPE_MSIX;
				add_new_irq(devpath, &hint);
				if (build_irq >= 0) {
					log(TO_CONSOLE, LOG_INFO, "Hotplug dev irq: %d finished.\n", irqnum);
					break;
				}
			}
		} while (entry != NULL);
		closedir(msidir);
		return;
	}

	sprintf(path, "%s/%s/irq", SYSPCI_DIR, dirname);
	if (process_one_line(path, get_int, &irqnum) < 0)
		goto done;

	/*
	 * no pci device has irq 0
	 * irq 255 is invalid on x86/x64 architectures
	 */
#if defined(__i386__) || defined(__x86_64__)
	if (irqnum && irqnum != 255) {
#else
	if (irqnum) {
#endif
		/* If build_irq is valid, only add irq when it's number equals to  build_irq */
		if ((build_irq < 0) || (irqnum == build_irq)) {
			hint.irq = irqnum;
			hint.type = IRQ_TYPE_LEGACY;
			add_new_irq(devpath, &hint);
			if (build_irq >= 0)
				log(TO_CONSOLE, LOG_INFO, "Hotplug dev irq: %d finished.\n", irqnum);
		}
	}

done:
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

void free_cl_opts(void)
{
	g_list_free_full(cl_banned_modules, free);
	g_list_free_full(cl_banned_irqs, free);
}

static void add_missing_irq(struct irq_info *info, void *data __attribute__((unused)))
{

	add_new_irq(NULL, info);
}

static void free_tmp_irqs(gpointer data)
{
	struct irq_info *info = data;

	free(info->name);
	free(info);
}

static struct irq_info * build_dev_irqs(int build_irq)
{
	DIR *devdir;
	struct dirent *entry;
	struct irq_info *new_irq = NULL;

	devdir = opendir(SYSPCI_DIR);
	if (devdir) {
		do {
			entry = readdir(devdir);
			if (!entry)
				break;
			/* when hotplug irqs, we add one irq at one time */
			build_one_dev_entry(entry->d_name, build_irq);
			if (build_irq >= 0) {
				new_irq = get_irq_info(build_irq);
				if (new_irq)
					break;
			}
		} while (entry != NULL);
		closedir(devdir);
	}
	return new_irq;
}

int proc_irq_hotplug(char *savedline, int irq, struct irq_info **pinfo)
{
	struct irq_info tmp_info = {0};

	/* firstly, init irq info by read device info */
	*pinfo = build_dev_irqs(irq);
	if (*pinfo == NULL) {
		/* secondly, init irq info by parse savedline */
		init_irq_class_and_type(savedline, &tmp_info, irq);
		add_new_irq(NULL, &tmp_info);
		free(tmp_info.name);

		*pinfo = get_irq_info(irq);
	}
	if (*pinfo == NULL) {
		return -1;
	}

	force_rebalance_irq(*pinfo, NULL);
	return 0;
}

void rebuild_irq_db(void)
{
	GList *tmp_irqs = NULL;

	free_irq_db();

	tmp_irqs = collect_full_irq_list();
	
	build_dev_irqs(-1);

	for_each_irq(tmp_irqs, add_missing_irq, NULL);
	g_list_free_full(tmp_irqs, free_tmp_irqs);

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

	if (a->class < b->class)
		return 1;
	if (a->class > b->class)
		return -1;
	if (a->load < b->load)
		return 1;
	if (a->load > b->load)
		return -1;
	if (a < b)
		return 1;
        return -1;
}

void sort_irq_list(GList **list)
{
	*list = g_list_sort(*list, sort_irqs);
}

static void remove_no_existing_irq(struct irq_info *info, void *data __attribute__((unused)))
{
	GList *entry = NULL;

	if (info->existing) {
		/* clear existing flag for next detection */
		info->existing = 0;
		return;
	}

	entry = g_list_find_custom(interrupts_db, info, compare_ints);
	if (entry) {
		interrupts_db = g_list_delete_link(interrupts_db, entry);
		log(TO_CONSOLE, LOG_INFO, "IRQ %d is removed from interrupts_db.\n", info->irq);
	}

	entry = g_list_find_custom(banned_irqs, info, compare_ints);
	if (entry) {
		banned_irqs = g_list_delete_link(banned_irqs, entry);
		log(TO_CONSOLE, LOG_INFO, "IRQ %d is removed from banned_irqs.\n", info->irq);
	}

	entry = g_list_find_custom(rebalance_irq_list, info, compare_ints);
	if (entry)
		rebalance_irq_list = g_list_delete_link(rebalance_irq_list, entry);

	if(info->assigned_obj) {
		entry = g_list_find_custom(info->assigned_obj->interrupts, info, compare_ints);
	    if (entry) {
			info->assigned_obj->interrupts = g_list_delete_link(info->assigned_obj->interrupts, entry);
		}
	}
	free_irq(info, NULL);
}

void clear_no_existing_irqs(void)
{
	for_each_irq(NULL, remove_no_existing_irq, NULL);
	if (banned_irqs) {
		for_each_irq(banned_irqs, remove_no_existing_irq, NULL);
	}
}

