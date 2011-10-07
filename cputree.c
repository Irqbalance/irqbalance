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

#include "config.h"
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

cpumask_t cpu_possible_map;

/* 
   it's convenient to have the complement of banned_cpus available so that 
   the AND operator can be used to mask out unwanted cpus
*/
static cpumask_t unbanned_cpus;

static struct package* add_cache_domain_to_package(struct cache_domain *cache, 
						    cpumask_t package_mask)
{
	GList *entry;
	struct package *package;
	struct cache_domain *lcache; 

	entry = g_list_first(packages);

	while (entry) {
		package = entry->data;
		if (cpus_equal(package_mask, package->common.mask))
			break;
		entry = g_list_next(entry);
	}

	if (!entry) {
		package = calloc(sizeof(struct package), 1);
		if (!package)
			return NULL;
		package->common.mask = package_mask;
		packages = g_list_append(packages, package);
		package_count++;
	}

	entry = g_list_first(package->cache_domains);
	while (entry) {
		lcache = entry->data;
		if (lcache == cache)
			break;
		entry = g_list_next(entry);
	}

	if (!entry) {
		package->cache_domains = g_list_append(package->cache_domains, cache);
		cache->package = package;
	}

	return package;
}
static struct cache_domain* add_cpu_to_cache_domain(struct cpu_core *cpu,
						    cpumask_t cache_mask)
{
	GList *entry;
	struct cache_domain *cache;
	struct cpu_core *lcpu;

	entry = g_list_first(cache_domains);

	while (entry) {
		cache = entry->data;
		if (cpus_equal(cache_mask, cache->common.mask))
			break;
		entry = g_list_next(entry);
	}

	if (!entry) {
		cache = calloc(sizeof(struct cache_domain), 1);
		if (!cache)
			return NULL;
		cache->common.mask = cache_mask;
		cache->common.number = cache_domain_count;
		cache_domains = g_list_append(cache_domains, cache);
		cache_domain_count++;
	}

	entry = g_list_first(cache->cpu_cores);
	while (entry) {
		lcpu = entry->data;
		if (lcpu == cpu)
			break;
		entry = g_list_next(entry);
	}

	if (!entry) {
		cache->cpu_cores = g_list_append(cache->cpu_cores, cpu);
		cpu->cache_domain = cache;
	}

	return cache;
}
 
static void do_one_cpu(char *path)
{
	struct cpu_core *cpu;
	FILE *file;
	char new_path[PATH_MAX];
	cpumask_t cache_mask, package_mask;
	struct cache_domain *cache;
	struct package *package;
	DIR *dir;
	struct dirent *entry;
	int nodeid;

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

	cpu->common.number = strtoul(&path[27], NULL, 10);

	cpu_set(cpu->common.number, cpu_possible_map);
	
	cpu_set(cpu->common.number, cpu->common.mask);

	/* if the cpu is on the banned list, just don't add it */
	if (cpus_intersects(cpu->common.mask, banned_cpus)) {
		free(cpu);
		/* even though we don't use the cpu we do need to count it */
		core_count++;
		return;
	}


	/* try to read the package mask; if it doesn't exist assume solitary */
	snprintf(new_path, PATH_MAX, "%s/topology/core_siblings", path);
	file = fopen(new_path, "r");
	cpu_set(cpu->common.number, package_mask);
	if (file) {
		char *line = NULL;
		size_t size = 0;
		if (getline(&line, &size, file)) 
			cpumask_parse_user(line, strlen(line), package_mask);
		fclose(file);
		free(line);
	}

	/* try to read the cache mask; if it doesn't exist assume solitary */
	/* We want the deepest cache level available so try index1 first, then index2 */
	cpu_set(cpu->common.number, cache_mask);
	snprintf(new_path, PATH_MAX, "%s/cache/index1/shared_cpu_map", path);
	file = fopen(new_path, "r");
	if (file) {
		char *line = NULL;
		size_t size = 0;
		if (getline(&line, &size, file)) 
			cpumask_parse_user(line, strlen(line), cache_mask);
		fclose(file);
		free(line);
	}
	snprintf(new_path, PATH_MAX, "%s/cache/index2/shared_cpu_map", path);
	file = fopen(new_path, "r");
	if (file) {
		char *line = NULL;
		size_t size = 0;
		if (getline(&line, &size, file)) 
			cpumask_parse_user(line, strlen(line), cache_mask);
		fclose(file);
		free(line);
	}

	nodeid=0;
	dir = opendir(path);
	do {
		entry = readdir(dir);
		if (!entry)
			break;
		if (strstr(entry->d_name, "node")) {
			nodeid = strtoul(&entry->d_name[4], NULL, 10);
			break;
		}
	} while (entry);
	closedir(dir);

	cache = add_cpu_to_cache_domain(cpu, cache_mask);
	package = add_cache_domain_to_package(cache, package_mask);
	add_package_to_node(package, nodeid);	
 
	/* 
	   blank out the banned cpus from the various masks so that interrupts
	   will never be told to go there
	 */
	cpus_and(cpu_cache_domain(cpu)->common.mask, cpu_cache_domain(cpu)->common.mask, unbanned_cpus);
	cpus_and(cpu_package(cpu)->common.mask, cpu_package(cpu)->common.mask, unbanned_cpus);
	cpus_and(cpu->common.mask, cpu->common.mask, unbanned_cpus);

	cpus = g_list_append(cpus, cpu);
	core_count++;
}

static void dump_irq(struct irq_info *info, void *data)
{
	int spaces = (long int)data;
	int i;
	for (i=0; i<spaces; i++) printf(" ");
	printf("Interrupt %i node_num is %d (%s/%u) \n", info->irq, irq_numa_node(info)->common.number, classes[info->class], (unsigned int)info->load);
}

static void dump_cpu_core(struct common_obj_data *d, void *data __attribute__((unused)))
{
	struct cpu_core *c = (struct cpu_core *)d;
	printf("                CPU number %i  numa_node is %d (load %lu)\n", c->common.number, cpu_numa_node(c)->common.number , (unsigned long)c->common.load);
	if (c->common.interrupts)
		for_each_irq(c->common.interrupts, dump_irq, (void *)18);
}

static void dump_cache_domain(struct common_obj_data *d, void *data)
{
	struct cache_domain *c = (struct cache_domain *)d;
	char *buffer = data;
	cpumask_scnprintf(buffer, 4095, c->common.mask);
	printf("        Cache domain %i:  numa_node is %d cpu mask is %s  (load %lu) \n", c->common.number, cache_domain_numa_node(c)->common.number, buffer, (unsigned long)c->common.load);
	if (c->cpu_cores)
		for_each_cpu_core(c->cpu_cores, dump_cpu_core, NULL);
	if (c->common.interrupts)
		for_each_irq(c->common.interrupts, dump_irq, (void *)10);
}

static void dump_package(struct common_obj_data *d, void *data)
{
	struct package *p = (struct package *)d;
	char *buffer = data;
	cpumask_scnprintf(buffer, 4096, p->common.mask);
	printf("Package %i:  numa_node is %d cpu mask is %s (load %lu)\n", p->common.number, package_numa_node(p)->common.number, buffer, (unsigned long)p->common.load);
	if (p->cache_domains)
		for_each_cache_domain(p->cache_domains, dump_cache_domain, buffer);
	if (p->common.interrupts)
		for_each_irq(p->common.interrupts, dump_irq, (void *)2);
}

void dump_tree(void)
{
	char buffer[4096];
	for_each_package(NULL, dump_package, buffer);
}

static void clear_cpu_stats(struct common_obj_data *d, void *data __attribute__((unused)))
{
	struct cpu_core *c = (struct cpu_core *)d;
	c->common.load = 0;
	c->irq_load = 0;
	c->softirq_load = 0;
}

static void clear_cd_stats(struct common_obj_data *d, void *data __attribute__((unused)))
{
	struct cache_domain *c = (struct cache_domain *)d;
	c->common.load = 0;
	for_each_cpu_core(c->cpu_cores, clear_cpu_stats, NULL);
}

static void clear_package_stats(struct common_obj_data *d, void *data __attribute__((unused)))
{
	struct package *p = (struct package *)d;
	p->common.load = 0;
	for_each_cache_domain(p->cache_domains, clear_cd_stats, NULL);
}

static void clear_node_stats(struct common_obj_data *d, void *data __attribute__((unused)))
{
	struct numa_node *n = (struct numa_node *)d;
	n->common.load = 0;
	for_each_package(n->packages, clear_package_stats, NULL);
}

static void clear_irq_stats(struct irq_info *info, void *data __attribute__((unused)))
{
	info->load = 0;
}

/*
 * this function removes previous state from the cpu tree, such as
 * which level does how much work and the actual lists of interrupts 
 * assigned to each component
 */
void clear_work_stats(void)
{
	for_each_numa_node(NULL, clear_node_stats, NULL);
	for_each_irq(NULL, clear_irq_stats, NULL);
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
			/*
 			 * We only want to count real cpus, not cpufreq and
 			 * cpuidle
 			 */
			if ((entry->d_name[3] < 0x30) | (entry->d_name[3] > 0x39))
				continue;
			sprintf(new_path, "/sys/devices/system/cpu/%s", entry->d_name);
			do_one_cpu(new_path);
		}
	} while (entry);
	closedir(dir);  

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
		g_list_free(package->common.interrupts);
		free(package);
		packages = g_list_delete_link(packages, item);
	}
	package_count = 0;

	while (cache_domains) {
		item = g_list_first(cache_domains);
		cache_domain = item->data;
		g_list_free(cache_domain->cpu_cores);
		g_list_free(cache_domain->common.interrupts);
		free(cache_domain);
		cache_domains = g_list_delete_link(cache_domains, item);
	}
	cache_domain_count = 0;


	while (cpus) {
		item = g_list_first(cpus);
		cpu = item->data;
		g_list_free(cpu->common.interrupts);
		free(cpu);
		cpus = g_list_delete_link(cpus, item);
	}
	core_count = 0;

}


void for_each_package(GList *list, void (*cb)(struct common_obj_data *p, void *data), void *data)
{
	GList *entry = g_list_first(list ? list : packages);
	GList *next;

	while (entry) {
		next = g_list_next(entry);
		cb(entry->data, data);
		entry = next;
	}
}

void for_each_cache_domain(GList *list, void (*cb)(struct common_obj_data *c, void *data), void *data)
{
	GList *entry = g_list_first(list ? list : cache_domains);
	GList *next;

	while (entry) {
		next = g_list_next(entry);
		cb(entry->data, data);
		entry = next;
	}
}

void for_each_cpu_core(GList *list, void (*cb)(struct common_obj_data *c, void *data), void *data)
{
	GList *entry = g_list_first(list ? list : cpus);
	GList *next;

	while (entry) {
		next = g_list_next(entry);
		cb(entry->data, data);
		entry = next;
	}
}

static gint compare_cpus(gconstpointer a, gconstpointer b)
{
	const struct cpu_core *ai = a;
	const struct cpu_core *bi = b;

	return ai->common.number - bi->common.number;	
}

struct cpu_core *find_cpu_core(int cpunr)
{
	GList *entry;
	struct cpu_core find;

	find.common.number = cpunr;
	entry = g_list_find_custom(cpus, &find, compare_cpus);

	return entry ? entry->data : NULL;
}	

int get_cpu_count(void)
{
	return g_list_length(cpus);
}

