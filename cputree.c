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

/*
 * This file contains the code to construct and manipulate a hierarchy of processors,
 * cache domains and processor cores.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>

#include <glib.h>

#include "irqbalance.h"


GList *cpus;
GList *cache_domains;
GList *packages;

int package_count;
int cache_domain_count;
int core_count;

/* Users want to be able to keep interrupts away from some cpus; store these in a cpumask_t */
cpumask_t banned_cpus;


/* 
   it's convenient to have the complement of banned_cpus available so that 
   the AND operator can be used to mask out unwanted cpus
*/
static cpumask_t unbanned_cpus;

static void fill_packages(void)
{
	GList *entry;

	entry = g_list_first(cache_domains);
	while (entry) {
		struct package *package;
		struct cache_domain *cache = NULL;
		GList *entry2;

		cache = entry->data;
		entry2 = entry;
		entry = g_list_next(entry);
		if (cache->marker) 
			continue;
		package = malloc(sizeof(struct package));
		if (!package)
			break;
		memset(package, 0, sizeof(struct package));
		package->mask = cache->package_mask;
		package->number = cache->number;
		while (entry2) {
			struct cache_domain *cache2;
			cache2 = entry2->data;
			if (cpus_equal(cache->package_mask, cache2->package_mask)) {
				cache2->marker = 1;
				package->cache_domains = g_list_append(package->cache_domains, cache2);
				if (package->number > cache2->number)
					package->number = cache2->number;
			}
			entry2 = g_list_next(entry2);
		}
		packages = g_list_append(packages, package);
		package_count++;
	}
}

static void fill_cache_domain(void)
{
	GList *entry;

	entry = g_list_first(cpus);
	while (entry) {
		struct cache_domain *cache = NULL;
		struct cpu_core *cpu;
		GList *entry2;
		cpu = entry->data;
		entry2 = entry;
		entry = g_list_next(entry);
		if (cpu->marker) 
			continue;
		cache = malloc(sizeof(struct cache_domain));
		if (!cache)
			break;
		memset(cache, 0, sizeof(struct cache_domain));
		cache->mask = cpu->cache_mask;
		cache->package_mask = cpu->package_mask;
		cache->number = cpu->number;
		cache_domains = g_list_append(cache_domains, cache);
		cache_domain_count++;
		while (entry2) {
			struct cpu_core *cpu2;
			cpu2 = entry2->data;
			if (cpus_equal(cpu->cache_mask, cpu2->cache_mask) && 
			    cpus_equal(cpu->package_mask, cpu2->package_mask)) {
				cpu2->marker = 1;
				cache->cpu_cores = g_list_append(cache->cpu_cores, cpu2);
				if (cpu2->number < cache->number)
					cache->number = cpu2->number;
			}
			entry2 = g_list_next(entry2);
		}
	}
}


static void do_one_cpu(char *path)
{
	struct cpu_core *cpu;
	FILE *file;
	char new_path[PATH_MAX];

	/* skip offline cpus */
	snprintf(new_path, PATH_MAX, "%s/online", path);
	file = fopen(new_path, "r");
	if (file) {
		char *line = NULL;
		size_t size = 0;
		if (getline(&line, &size, file)==0)
			return;
		fclose(file);
		if (line && line[0]=='0') {
			free(line);
			return;
		}
		free(line);
	}

	cpu = malloc(sizeof(struct cpu_core));
	if (!cpu)
		return;
	memset(cpu, 0, sizeof(struct cpu_core));

	cpu->number = strtoul(&path[27], NULL, 10);
	
	cpu_set(cpu->number, cpu->mask);

	/* if the cpu is on the banned list, just don't add it */
	if (cpus_intersects(cpu->mask, banned_cpus)) {
		free(cpu);
		/* even though we don't use the cpu we do need to count it */
		core_count++;
		return;
	}


	/* try to read the package mask; if it doesn't exist assume solitary */
	snprintf(new_path, PATH_MAX, "%s/topology/core_siblings", path);
	file = fopen(new_path, "r");
	cpu_set(cpu->number, cpu->package_mask);
	if (file) {
		char *line = NULL;
		size_t size = 0;
		if (getline(&line, &size, file)) 
			cpumask_parse_user(line, strlen(line), cpu->package_mask);
		fclose(file);
		free(line);
	}

	/* try to read the cache mask; if it doesn't exist assume solitary */
	/* We want the deepest cache level available so try index1 first, then index2 */
	cpu_set(cpu->number, cpu->cache_mask);
	snprintf(new_path, PATH_MAX, "%s/cache/index1/shared_cpu_map", path);
	file = fopen(new_path, "r");
	if (file) {
		char *line = NULL;
		size_t size = 0;
		if (getline(&line, &size, file)) 
			cpumask_parse_user(line, strlen(line), cpu->cache_mask);
		fclose(file);
		free(line);
	}
	snprintf(new_path, PATH_MAX, "%s/cache/index2/shared_cpu_map", path);
	file = fopen(new_path, "r");
	if (file) {
		char *line = NULL;
		size_t size = 0;
		if (getline(&line, &size, file)) 
			cpumask_parse_user(line, strlen(line), cpu->cache_mask);
		fclose(file);
		free(line);
	}

	/* 
	   blank out the banned cpus from the various masks so that interrupts
	   will never be told to go there
	 */
	cpus_and(cpu->cache_mask, cpu->cache_mask, unbanned_cpus);
	cpus_and(cpu->package_mask, cpu->package_mask, unbanned_cpus);
	cpus_and(cpu->mask, cpu->mask, unbanned_cpus);

	cpus = g_list_append(cpus, cpu);
	core_count++;
}

static void dump_irqs(int spaces, GList *interrupts)
{
	struct interrupt *irq;
	while (interrupts) {
		int i;
		for (i=0; i<spaces;i++) printf(" ");
		irq = interrupts->data;
		printf("Interrupt %i (%s/%u) \n", irq->number, classes[irq->class], (unsigned int)irq->workload);
		interrupts = g_list_next(interrupts);
	}
}

void dump_tree(void)
{
	GList *p_iter, *c_iter, *cp_iter;
	struct package *package;
	struct cache_domain *cache_domain;
	struct cpu_core *cpu;

	char buffer[4096];
	p_iter = g_list_first(packages);
	while (p_iter) {
		package = p_iter->data;
		cpumask_scnprintf(buffer, 4096, package->mask);
		printf("Package %i:  cpu mask is %s (workload %lu)\n", package->number, buffer, (unsigned long)package->workload);
		c_iter = g_list_first(package->cache_domains);
		while (c_iter) {
			cache_domain = c_iter->data;
			c_iter = g_list_next(c_iter);
			cpumask_scnprintf(buffer, 4095, cache_domain->mask);
			printf("        Cache domain %i: cpu mask is %s  (workload %lu) \n", cache_domain->number, buffer, (unsigned long)cache_domain->workload);
			cp_iter = cache_domain->cpu_cores;
			while (cp_iter) {
				cpu = cp_iter->data;
				cp_iter = g_list_next(cp_iter);
				printf("                CPU number %i  (workload %lu)\n", cpu->number, (unsigned long)cpu->workload);
				dump_irqs(18, cpu->interrupts);
			}
			dump_irqs(10, cache_domain->interrupts);
		}
		dump_irqs(2, package->interrupts);
		p_iter = g_list_next(p_iter);
	}
}

/*
 * this function removes previous state from the cpu tree, such as
 * which level does how much work and the actual lists of interrupts 
 * assigned to each component
 */
void clear_work_stats(void)
{
	GList *p_iter, *c_iter, *cp_iter;
	struct package *package;
	struct cache_domain *cache_domain;
	struct cpu_core *cpu;

	p_iter = g_list_first(packages);
	while (p_iter) {
		package = p_iter->data;
		package->workload = 0;
		g_list_free(package->interrupts);
		package->interrupts = NULL;
		c_iter = g_list_first(package->cache_domains);
		memset(package->class_count, 0, sizeof(package->class_count));
		while (c_iter) {
			cache_domain = c_iter->data;
			c_iter = g_list_next(c_iter);
			cache_domain->workload = 0;
			cp_iter = cache_domain->cpu_cores;
			g_list_free(cache_domain->interrupts);
			cache_domain->interrupts = NULL;
			memset(cache_domain->class_count, 0, sizeof(cache_domain->class_count));
			while (cp_iter) {
				cpu = cp_iter->data;
				cp_iter = g_list_next(cp_iter);
				cpu->workload = 0;
				g_list_free(cpu->interrupts);
				cpu->interrupts = NULL;
				memset(cpu->class_count, 0, sizeof(cpu->class_count));
			}
		}
		p_iter = g_list_next(p_iter);
	}
}


void parse_cpu_tree(void)
{
	DIR *dir;
	struct dirent *entry;

	cpus_complement(unbanned_cpus, banned_cpus);

	dir = opendir("/sys/devices/system/cpu");
	if (!dir)
		return;
	do {
		entry = readdir(dir);
                if (entry && strlen(entry->d_name)>3 && strstr(entry->d_name,"cpu")) {
			char new_path[PATH_MAX];
			sprintf(new_path, "/sys/devices/system/cpu/%s", entry->d_name);
			do_one_cpu(new_path);
		}
	} while (entry);
	closedir(dir);  

	fill_cache_domain();
	fill_packages();

	if (debug_mode)
		dump_tree();

}


/*
 * This function frees all memory related to a cpu tree so that a new tree
 * can be read
 */
void clear_cpu_tree(void)
{
	GList *item;
	struct cpu_core *cpu;
	struct cache_domain *cache_domain;
	struct package *package;

	while (packages) {
		item = g_list_first(packages);
		package = item->data;
		g_list_free(package->cache_domains);
		g_list_free(package->interrupts);
		free(package);
		packages = g_list_delete_link(packages, item);
	}
	package_count = 0;

	while (cache_domains) {
		item = g_list_first(cache_domains);
		cache_domain = item->data;
		g_list_free(cache_domain->cpu_cores);
		g_list_free(cache_domain->interrupts);
		free(cache_domain);
		cache_domains = g_list_delete_link(cache_domains, item);
	}
	cache_domain_count = 0;


	while (cpus) {
		item = g_list_first(cpus);
		cpu = item->data;
		g_list_free(cpu->interrupts);
		free(cpu);
		cpus = g_list_delete_link(cpus, item);
	}
	core_count = 0;

}
