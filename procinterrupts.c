/* 
 * Copyright (C) 2006, Intel Corporation
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

#include "cpumask.h"
#include "irqbalance.h"

#define LINESIZE 4096

static int proc_int_has_msi = 0;
static int msi_found_in_sysfs = 0;

void parse_proc_interrupts(void)
{
	FILE *file;
	char *line = NULL;
	size_t size = 0;

	file = fopen("/proc/interrupts", "r");
	if (!file)
		return;

	/* first line is the header we don't need; nuke it */
	if (getline(&line, &size, file)==0) {
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

		if (getline(&line, &size, file)==0)
			break;

		if (!proc_int_has_msi)
			if (strstr(line, "MSI") != NULL)
				proc_int_has_msi = 1;

		/* lines with letters in front are special, like NMI count. Ignore */
		if (!(line[0]==' ' || (line[0]>='0' && line[0]<='9')))
			break;
		c = strchr(line, ':');
		if (!c)
			continue;
		*c = 0;
		c++;
		number = strtoul(line, NULL, 10);
		info = get_irq_info(number);
		if (!info)
			info = add_misc_irq(number);

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
		if (cpunr != core_count) 
			need_cpu_rescan = 1;
		
		info->irq_count = count;

		/* is interrupt MSI based? */
		if ((info->type == IRQ_TYPE_MSI) || (info->type == IRQ_TYPE_MSIX))
			msi_found_in_sysfs = 1;
	}		
	if ((proc_int_has_msi) && (!msi_found_in_sysfs)) {
		syslog(LOG_WARNING, "WARNING: MSI interrupts found in /proc/interrupts\n");
		syslog(LOG_WARNING, "But none found in sysfs, you need to update your kernel\n");
		syslog(LOG_WARNING, "Until then, IRQs will be improperly classified\n");
		/*
 		 * Set msi_foun_in_sysfs, so we don't get this error constantly
 		 */
		msi_found_in_sysfs = 1;
	}
	fclose(file);
	free(line);
}


static void accumulate_irq_count(struct irq_info *info, void *data)
{
	uint64_t *acc = data;

	*acc += (info->irq_count - info->last_irq_count);
}

static void assign_load_slice(struct irq_info *info, void *data)
{
	uint64_t *load_slice = data;
	info->load = (info->irq_count - info->last_irq_count) * *load_slice;
}

static void compute_irq_load_share(struct common_obj_data *d, void *data __attribute__((unused)))
{
	struct cpu_core *cpu = (struct cpu_core *)d;
	uint64_t total_irq_counts = 0;
	uint64_t load_slice;

	for_each_irq(cpu->common.interrupts, accumulate_irq_count, &total_irq_counts);

	load_slice = cpu->common.load / total_irq_counts;

	for_each_irq(cpu->common.interrupts, assign_load_slice, &load_slice);
}

void parse_proc_stat()
{
	FILE *file;
	char *line = NULL;
	size_t size = 0;
	int cpunr, rc, cpucount;
	struct cpu_core *cpu;
	int irq_load, softirq_load;

	file = fopen("/proc/stat", "r");
	if (!file) {
		syslog(LOG_WARNING, "WARNING cant open /proc/stat.  balacing is broken\n");
		return;
	}

	/* first line is the header we don't need; nuke it */
	if (getline(&line, &size, file)==0) {
		free(line);
		syslog(LOG_WARNING, "WARNING read /proc/stat. balancing is broken\n");
		fclose(file);
		return;
        }

	cpucount = 0;
	while (!feof(file)) {
		if (getline(&line, &size, file)==0)
                        break;

		if (!strstr(line, "cpu"))
			break;

		cpunr = strtoul(&line[3], NULL, 10);

		rc = sscanf(line, "%*s %*d %*d %*d %*d %*d %d %d", &irq_load, &softirq_load);
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
		cpu->irq_load = irq_load;
		cpu->softirq_load = softirq_load;
		cpu->common.load = irq_load + softirq_load;
		cpu->cache_domain->common.load += cpu->common.load;
		cpu->cache_domain->package->common.load += cpu->common.load;
		cpu->cache_domain->package->numa_node->common.load += cpu->common.load;
	}

	fclose(file);
	if (cpucount != get_cpu_count()) {
		syslog(LOG_WARNING, "WARNING, didn't collect load info for all cpus, balancing is broken\n");
		return;
	}

	/*
 	 * Now that we have load for each cpu attribute a fair share of the load
 	 * to each irq on that cpu
 	 */
	for_each_cpu_core(NULL, compute_irq_load_share, NULL);

}
