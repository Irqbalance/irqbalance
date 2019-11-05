/* 
 * Copyright (C) 2006, Intel Corporation
 * Copyright (C) 2012, Neil Horman <nhorman@tuxdriver.com> 
 * 
 * This file is part of irqbalance
 *
 * This program file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program in a file named COPYING; if not, write to the 
 * Free Software Foundation, Inc., 
 * 51 Franklin Street, Fifth Floor, 
 * Boston, MA 02110-1301 USA
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <ctype.h>
#include <errno.h>

#include "cpumask.h"
#include "irqbalance.h"

#ifdef AARCH64
#include <sys/types.h>
#include <regex.h>
#include <dirent.h>
#endif

#define LINESIZE 4096

static int proc_int_has_msi = 0;
static int msi_found_in_sysfs = 0;

#ifdef AARCH64
struct irq_match {
	char *matchstring;
	regex_t rcomp;
	int (*refine_match)(char *name, struct irq_info *info);
	int type;
	int class;
};

static int check_platform_device(char *name, struct irq_info *info)
{
	DIR *dirfd;
	char path[512];
	struct dirent *ent;
	int rc = -ENOENT, i;
	static struct pdev_irq_info {
		char *d_name;
		int type;
		int class;
	} pdev_irq_info[] = {
		{"ata", IRQ_TYPE_LEGACY, IRQ_SCSI},
		{"net", IRQ_TYPE_LEGACY, IRQ_ETH},
		{"usb", IRQ_TYPE_LEGACY, IRQ_OTHER},
		{NULL},
	};

	memset(path, 0, 512);

	strcat(path, "/sys/devices/platform/");
	strcat(path, name);
	strcat(path, "/");
	dirfd = opendir(path);

	if (!dirfd) {
		log(TO_ALL, LOG_DEBUG, "No directory %s: %s\n", path, strerror(errno));
		return -ENOENT;
	}

	while ((ent = readdir(dirfd)) != NULL) {

		log(TO_ALL, LOG_DEBUG, "Checking entry %s\n", ent->d_name);
		for (i = 0; pdev_irq_info[i].d_name != NULL; i++) {
			if (!strncmp(ent->d_name, pdev_irq_info[i].d_name, strlen(pdev_irq_info[i].d_name))) {
				info->type = pdev_irq_info[i].type;
				info->class = pdev_irq_info[i].class;
				rc = 0;
				goto out;
			}
		}
	}

out:
	closedir(dirfd);
	log(TO_ALL, LOG_DEBUG, "IRQ %s is of type %d and class %d\n", name, info->type, info->class);
	return rc;

}

static void guess_arm_irq_hints(char *name, struct irq_info *info)
{
	int i, rc;
	static int compiled = 0;
	/* Note: Last entry is a catchall */
	static struct irq_match matches[] = {
		{ "eth.*" ,{NULL} ,NULL, IRQ_TYPE_LEGACY, IRQ_GBETH },
		{ "[A-Z0-9]{4}[0-9a-f]{4}", {NULL} ,check_platform_device, IRQ_TYPE_LEGACY, IRQ_OTHER},
		{ "PNP[0-9a-f]{4}", {NULL} ,check_platform_device, IRQ_TYPE_LEGACY, IRQ_OTHER},
		{ ".*", {NULL}, NULL, IRQ_TYPE_LEGACY, IRQ_OTHER},
		{NULL},
	};


	if (!compiled) {
		for (i=0; matches[i].matchstring != NULL; i++) {
			rc = regcomp(&matches[i].rcomp, matches[i].matchstring, REG_EXTENDED | REG_NOSUB);
			if (rc) {
				char errbuf[256];
				regerror(rc, &matches[i].rcomp, errbuf, 256);
				log(TO_ALL, LOG_WARNING, "WARNING: Failed to compile regex %s : %s\n",
				    matches[i].matchstring, errbuf);
				return;
			}
		}

		compiled = 1;
	}

	for (i=0; matches[i].matchstring != NULL; i++) {
		if (!regexec(&matches[i].rcomp, name, 0, NULL, 0)) {
			info->type = matches[i].type;
			info->class = matches[i].class;
			if (matches[i].refine_match)
			    matches[i].refine_match(name, info);
			log(TO_ALL, LOG_DEBUG, "IRQ %s(%d) guessed as class %d\n", name, info->irq,info->class);
			break;
		}	
	}
	
	
}
#endif

GList* collect_full_irq_list()
{
	GList *tmp_list = NULL;
	FILE *file;
	char *line = NULL;
	size_t size = 0;
	char *irq_name, *irq_mod, *savedptr, *last_token, *p;
#ifdef AARCH64
	char *tmp;
#endif

	file = fopen("/proc/interrupts", "r");
	if (!file)
		return NULL;

	/* first line is the header we don't need; nuke it */
	if (getline(&line, &size, file)<=0) {
		free(line);
		fclose(file);
		return NULL;
	}

	while (!feof(file)) {
		int	 number;
		int      is_xen_dyn = 0;
		struct irq_info *info;
		char *c;
		char *savedline = NULL;

		if (getline(&line, &size, file)<=0)
			break;

		/* lines with letters in front are special, like NMI count. Ignore */
		c = line;
		while (isblank(*(c)))
			c++;

		if (!isdigit(*c))
			break;
		c = strchr(line, ':');
		if (!c)
			continue;

		savedline = strdup(line);
		if (!savedline)
			break;
		irq_name = strtok_r(savedline, " ", &savedptr);
		if (strstr(irq_name, "xen-dyn") != NULL)
			is_xen_dyn = 1;
		last_token = strtok_r(NULL, " ", &savedptr);
		while ((p = strtok_r(NULL, " ", &savedptr))) {
			irq_name = last_token;
			if (strstr(irq_name, "xen-dyn") != NULL)
				is_xen_dyn = 1;
			last_token = p;
		}

#ifdef AARCH64
		/* Of course the formatting for /proc/interrupts is different on different arches */
		irq_name = last_token;
		tmp = strchr(irq_name, '\n');
		if (tmp)
			*tmp = 0;
#endif
		irq_mod = last_token;

		*c = 0;
		number = strtoul(line, NULL, 10);

		info = calloc(1, sizeof(struct irq_info));
		if (info) {
			info->irq = number;
			if (strstr(irq_name, "-event") != NULL && is_xen_dyn == 1) {
				info->type = IRQ_TYPE_VIRT_EVENT;
				info->class = IRQ_VIRT_EVENT;
			} else {
#ifdef AARCH64
				guess_arm_irq_hints(irq_name, info);
#else
				info->type = IRQ_TYPE_LEGACY;
				info->class = IRQ_OTHER;
#endif
			}
			info->name = strdup(irq_mod);
			tmp_list = g_list_append(tmp_list, info);
		}
		free(savedline);
	}
	fclose(file);
	free(line);
	return tmp_list;
}

void parse_proc_interrupts(void)
{
	FILE *file;
	char *line = NULL;
	size_t size = 0;

	file = fopen("/proc/interrupts", "r");
	if (!file)
		return;

	/* first line is the header we don't need; nuke it */
	if (getline(&line, &size, file)<=0) {
		free(line);
		fclose(file);
		return;
	}

	while (!feof(file)) {
		int cpunr;
		int	 number;
		uint64_t count;
		char *c, *c2;
		struct irq_info *info;

		if (getline(&line, &size, file)<=0)
			break;

		if (!proc_int_has_msi)
			if (strstr(line, "MSI") != NULL)
				proc_int_has_msi = 1;

		/* lines with letters in front are special, like NMI count. Ignore */
		c = line;
		while (isblank(*(c)))
			c++;
			
		if (!isdigit(*c))
			break;
		c = strchr(line, ':');
		if (!c)
			continue;

		*c = 0;
		c++;
		number = strtoul(line, NULL, 10);

		info = get_irq_info(number);
		if (!info) {
			need_rescan = 1;
			break;
		}

		count = 0;
		cpunr = 0;

		c2=NULL;
		while (1) {
			uint64_t C;
			C = strtoull(c, &c2, 10);
			if (c==c2) /* end of numbers */
				break;
			count += C;
			c=c2;
			cpunr++;
		}
		if (cpunr != num_online_cpus()) {
			need_rescan = 1;
			break;
		}

		/* IRQ removed and reinserted, need restart or this will
		 * cause an overflow and IRQ won't be rebalanced again
		 */
		if (count < info->irq_count) {
			need_rescan = 1;
			break;
		}

		info->last_irq_count = info->irq_count;		
		info->irq_count = count;

		/* is interrupt MSI based? */
		if ((info->type == IRQ_TYPE_MSI) || (info->type == IRQ_TYPE_MSIX))
			msi_found_in_sysfs = 1;
	}		
	if ((proc_int_has_msi) && (!msi_found_in_sysfs) && (!need_rescan)) {
		log(TO_ALL, LOG_WARNING, "WARNING: MSI interrupts found in /proc/interrupts\n");
		log(TO_ALL, LOG_WARNING, "But none found in sysfs, you need to update your kernel\n");
		log(TO_ALL, LOG_WARNING, "Until then, IRQs will be improperly classified\n");
		/*
 		 * Set msi_foun_in_sysfs, so we don't get this error constantly
 		 */
		msi_found_in_sysfs = 1;
	}
	fclose(file);
	free(line);
}


static void assign_load_slice(struct irq_info *info, void *data)
{
	uint64_t *load_slice = data;
	info->load = (info->irq_count - info->last_irq_count) * *load_slice;

	/*
 	 * Every IRQ has at least a load of 1
 	 */
	if (!info->load)
		info->load++;
}

/*
 * Recursive helper to estimate the number of irqs shared between 
 * multiple topology objects that was handled by this particular object
 */
static uint64_t get_parent_branch_irq_count_share(struct topo_obj *d)
{
	uint64_t total_irq_count = 0;

	if (d->parent) {
		total_irq_count = get_parent_branch_irq_count_share(d->parent);
		total_irq_count /= g_list_length((d->parent)->children);
	}

	total_irq_count += d->irq_count;

	return total_irq_count;
}

static void get_children_branch_irq_count(struct topo_obj *d, void *data)
{
	uint64_t *total_irq_count = data;

	if (g_list_length(d->children) > 0)
		for_each_object(d->children, get_children_branch_irq_count, total_irq_count);

	*total_irq_count += d->irq_count;
}

static void compute_irq_branch_load_share(struct topo_obj *d, void *data __attribute__((unused)))
{
	uint64_t local_irq_counts = 0;
	uint64_t load_slice;

	if (g_list_length(d->interrupts) > 0) {
		local_irq_counts = get_parent_branch_irq_count_share(d);
		if (g_list_length(d->children) > 0)
			for_each_object(d->children, get_children_branch_irq_count, &local_irq_counts);
		load_slice = local_irq_counts ? (d->load / local_irq_counts) : 1;
		for_each_irq(d->interrupts, assign_load_slice, &load_slice);
	}

}

static void accumulate_irq_count(struct irq_info *info, void *data)
{
	uint64_t *acc = data;

	*acc += (info->irq_count - info->last_irq_count);
}

static void accumulate_interrupts(struct topo_obj *d, void *data __attribute__((unused)))
{
	if (g_list_length(d->children) > 0) {
		for_each_object(d->children, accumulate_interrupts, NULL);
	}

	d->irq_count = 0;
	if (g_list_length(d->interrupts) > 0)
		for_each_irq(d->interrupts, accumulate_irq_count, &(d->irq_count));
}

static void accumulate_load(struct topo_obj *d, void *data)
{
	uint64_t *load = data;

	*load += d->load;
}

static void set_load(struct topo_obj *d, void *data __attribute__((unused)))
{
	if (g_list_length(d->children) > 0) {
		for_each_object(d->children, set_load, NULL);
		d->load = 0;
		for_each_object(d->children, accumulate_load, &(d->load));
	}
}

void parse_proc_stat(void)
{
	FILE *file;
	char *line = NULL;
	size_t size = 0;
	int cpunr, rc, cpucount;
	struct topo_obj *cpu;
	unsigned long long irq_load, softirq_load;

	file = fopen("/proc/stat", "r");
	if (!file) {
		log(TO_ALL, LOG_WARNING, "WARNING cant open /proc/stat.  balancing is broken\n");
		return;
	}

	/* first line is the header we don't need; nuke it */
	if (getline(&line, &size, file)<=0) {
		free(line);
		log(TO_ALL, LOG_WARNING, "WARNING read /proc/stat. balancing is broken\n");
		fclose(file);
		return;
	}

	cpucount = 0;
	while (!feof(file)) {
		if (getline(&line, &size, file)<=0)
			break;

		if (!strstr(line, "cpu"))
			break;

		cpunr = strtoul(&line[3], NULL, 10);

		if (cpu_isset(cpunr, banned_cpus))
			continue;

		rc = sscanf(line, "%*s %*u %*u %*u %*u %*u %llu %llu", &irq_load, &softirq_load);
		if (rc < 2)
			break;	

		cpu = find_cpu_core(cpunr);

		if (!cpu)
			break;

		cpucount++;

		/*
 		 * For each cpu add the irq and softirq load and propagate that
 		 * all the way up the device tree
 		 */
		if (cycle_count) {
			cpu->load = (irq_load + softirq_load) - (cpu->last_load);
			/*
			 * the [soft]irq_load values are in jiffies, with
			 * HZ jiffies per second.  Convert the load to nanoseconds
			 * to get a better integer resolution of nanoseconds per
			 * interrupt.
			 */
			cpu->load *= NSEC_PER_SEC/HZ;
		}
		cpu->last_load = (irq_load + softirq_load);
	}

	fclose(file);
	free(line);
	if (cpucount != get_cpu_count()) {
		log(TO_ALL, LOG_WARNING, "WARNING, didn't collect load info for all cpus, balancing is broken\n");
		return;
	}

	/*
 	 * Set the load values for all objects above cpus
 	 */
	for_each_object(numa_nodes, set_load, NULL);

	/*
 	 * Collect local irq_count on each object
 	 */
	for_each_object(numa_nodes, accumulate_interrupts, NULL);

	/*
 	 * Now that we have load for each cpu attribute a fair share of the load
 	 * to each irq on that cpu
 	 */
	for_each_object(cpus, compute_irq_branch_load_share, NULL);
	for_each_object(cache_domains, compute_irq_branch_load_share, NULL);
	for_each_object(packages, compute_irq_branch_load_share, NULL);
	for_each_object(numa_nodes, compute_irq_branch_load_share, NULL);

}
