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

/*
 * This file contains the code to construct and manipulate a hierarchy of processors,
 * cache domains and processor cores.
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <inttypes.h>

#include <glib.h>

#include "irqbalance.h"
#include "thermal.h"

#ifdef HAVE_IRQBALANCEUI
extern char *banned_cpumask_from_ui;
#endif
extern char *cpu_ban_string;

GList *cpus;
GList *cache_domains;
GList *packages;

int cache_domain_count;

/* Users want to be able to keep interrupts away from some cpus; store these in a cpumask_t */
cpumask_t banned_cpus;

cpumask_t cpu_online_map;

/* 
   it's convenient to have the complement of banned_cpus available so that 
   the AND operator can be used to mask out unwanted cpus
*/
cpumask_t unbanned_cpus;

int process_one_line(char *path, void (*cb)(char *line, void *data), void *data)
{
	FILE *file;
	char *line = NULL;
	size_t size = 0;
	int ret = -1;

	file = fopen(path, "r");
	if (!file)
		return ret;

	if (getline(&line, &size, file) > 0) {
		cb(line, data);
		ret = 0;
	}
	free(line);
	fclose(file);
	return ret;
}

void get_hex(char *line, void *data)
{
	*(int *)data = strtoul(line, NULL, 16);
}

void get_int(char *line, void *data)
{
	*(int *)data = strtoul(line, NULL, 10);
}

void get_mask_from_bitmap(char *line, void *mask)
{
	cpumask_parse_user(line, strlen(line), *(cpumask_t *)mask);
}

static void get_mask_from_cpulist(char *line, void *mask)
{
	if (strlen(line) && line[0] != '\n')
		cpulist_parse(line, strlen(line), *(cpumask_t *)mask);
}

/*
 * By default do not place IRQs on CPUs the kernel keeps isolated or
 * nohz_full, as specified through the boot commandline. Users can
 * override this with the IRQBALANCE_BANNED_CPULIST environment variable.
 */
static void setup_banned_cpus(void)
{
	char *path = NULL;
	char buffer[4096];
	cpumask_t nohz_full;
	cpumask_t isolated_cpus;
	char *env = NULL;

#ifdef HAVE_IRQBALANCEUI
	/* A manually specified cpumask overrides auto-detection. */
	if (cpu_ban_string != NULL && banned_cpumask_from_ui != NULL) {
		cpulist_parse(banned_cpumask_from_ui,
			strlen(banned_cpumask_from_ui), banned_cpus);
		goto out;
	}
#endif

	/*
	 * Notes:
	 * The IRQBALANCE_BANNED_CPUS will be discarded, please use
	 * IRQBALANCE_BANNED_CPULIST instead.
	 *
	 * Before deleting this environment variable, Introduce a
	 * deprecation period first for the consider of compatibility.
	 */
	env = getenv("IRQBALANCE_BANNED_CPUS");
	if (env && strlen(env))  {
		cpumask_parse_user(env, strlen(env), banned_cpus);
		log(TO_ALL, LOG_WARNING,
			"IRQBALANCE_BANNED_CPUS is discarded, Use IRQBALANCE_BANNED_CPULIST instead\n");
		goto out;
	}

	env = getenv("IRQBALANCE_BANNED_CPULIST");
	if (env && strlen(env)) {
		cpulist_parse(env, strlen(env), banned_cpus);
		goto out;
	}

	cpus_clear(isolated_cpus);
	cpus_clear(nohz_full);

	path = "/sys/devices/system/cpu/isolated";
	process_one_line(path, get_mask_from_cpulist, &isolated_cpus);

	path = "/sys/devices/system/cpu/nohz_full";
	process_one_line(path, get_mask_from_cpulist, &nohz_full);

	cpus_or(banned_cpus, nohz_full, isolated_cpus);

	cpumask_scnprintf(buffer, 4096, isolated_cpus);
	log(TO_CONSOLE, LOG_INFO, "Prevent irq assignment to these isolated CPUs: %s\n", buffer);
	cpumask_scnprintf(buffer, 4096, nohz_full);
	log(TO_CONSOLE, LOG_INFO, "Prevent irq assignment to these adaptive-ticks CPUs: %s\n", buffer);
out:
#ifdef HAVE_THERMAL
	cpus_or(banned_cpus, banned_cpus, thermal_banned_cpus);
	cpumask_scnprintf(buffer, 4096, thermal_banned_cpus);
	log(TO_CONSOLE, LOG_INFO, "Prevent irq assignment to these thermal-banned CPUs: %s\n", buffer);
#endif
	cpumask_scnprintf(buffer, 4096, banned_cpus);
	log(TO_CONSOLE, LOG_INFO, "Banned CPUs: %s\n", buffer);
}

static void add_numa_node_to_topo_obj(struct topo_obj *obj, int nodeid)
{
	GList *entry;
	struct topo_obj *node;

	node = get_numa_node(nodeid);
	if (!node || (numa_avail && (node->number == NUMA_NO_NODE)))
		return;

	entry = g_list_find(obj->numa_nodes, node);
	if (!entry)
		obj->numa_nodes = g_list_append(obj->numa_nodes, node);

	if (!numa_avail && obj->obj_type == OBJ_TYPE_PACKAGE) {
		entry = g_list_find(node->children, obj);
		if (!entry) {
			node->children = g_list_append(node->children, obj);
			obj->parent = node;
		}
	}
}

static struct topo_obj* add_cache_domain_to_package(struct topo_obj *cache,
						    int packageid,
						    cpumask_t package_mask,
						    int nodeid)
{
	GList *entry;
	struct topo_obj *package;

	entry = g_list_first(packages);

	while (entry) {
		package = entry->data;
		if (cpus_equal(package_mask, package->mask)) {
			if (packageid != package->number)
				log(TO_ALL, LOG_WARNING, "package_mask with different physical_package_id found!\n");
			break;
		}
		entry = g_list_next(entry);
	}

	if (!entry) {
		package = calloc(1, sizeof(struct topo_obj));
		if (!package) {
			need_rebuild = 1;
			return NULL;
		}
		package->mask = package_mask;
		package->obj_type = OBJ_TYPE_PACKAGE;
		package->obj_type_list = &packages;
		package->number = packageid;
		packages = g_list_append(packages, package);
	}

	entry = g_list_find(package->children, cache);
	if (!entry) {
		package->children = g_list_append(package->children, cache);
		cache->parent = package;
	}

	if (!numa_avail || (nodeid > NUMA_NO_NODE))
		add_numa_node_to_topo_obj(package, nodeid);

	return package;
}
static struct topo_obj* add_cpu_to_cache_domain(struct topo_obj *cpu,
						    cpumask_t cache_mask,
						    int nodeid)
{
	GList *entry;
	struct topo_obj *cache;

	entry = g_list_first(cache_domains);

	while (entry) {
		cache = entry->data;
		if (cpus_equal(cache_mask, cache->mask))
			break;
		entry = g_list_next(entry);
	}

	if (!entry) {
		cache = calloc(1, sizeof(struct topo_obj));
		if (!cache) {
			need_rebuild = 1;
			return NULL;
		}
		cache->obj_type = OBJ_TYPE_CACHE;
		cache->mask = cache_mask;
		cache->number = cache_domain_count;
		cache->obj_type_list = &cache_domains;
		cache_domains = g_list_append(cache_domains, cache);
		cache_domain_count++;
	}

	entry = g_list_find(cache->children, cpu);
	if (!entry) {
		cache->children = g_list_append(cache->children, cpu);
		cpu->parent = (struct topo_obj *)cache;
	}

	if (!numa_avail || (nodeid > NUMA_NO_NODE))
		add_numa_node_to_topo_obj(cache, nodeid);

	return cache;
}

#define ADJ_SIZE(r,s) PATH_MAX-strlen(r)-strlen(#s) 
static void do_one_cpu(char *path)
{
	struct topo_obj *cpu;
	char new_path[PATH_MAX];
	char *online_path ="/sys/devices/system/cpu/online";
	cpumask_t cache_mask, package_mask;
	struct topo_obj *cache;
	DIR *dir;
	struct dirent *entry;
	int nodeid;
	int packageid = 0;
	unsigned int max_cache_index, cache_index, cache_stat;
	cpumask_t online_cpus;
	char *cpunrptr = NULL;
	int cpunr = -1;

	/* skip offline cpus */
	cpus_clear(online_cpus);
	process_one_line(online_path, get_mask_from_cpulist, &online_cpus);
	/* Get the current cpu number from the path */
	cpunrptr = rindex(path, '/');
	cpunrptr += 4;
	cpunr = atoi(cpunrptr);
	if (!cpu_isset(cpunr, online_cpus))
		return;

	cpu = calloc(1, sizeof(struct topo_obj));
	if (!cpu) {
		need_rebuild = 1;
		return;
	}

	cpu->obj_type = OBJ_TYPE_CPU;

	cpu->number = strtoul(&path[27], NULL, 10);

	cpu_set(cpu->number, cpu_online_map);
	
	cpu_set(cpu->number, cpu->mask);

	/*
 	 * Default the cache_domain mask to be equal to the cpu
 	 */
	cpus_clear(cache_mask);
	cpu_set(cpu->number, cache_mask);

	/* if the cpu is on the banned list, just don't add it */
	if (cpus_intersects(cpu->mask, banned_cpus)) {
		free(cpu);
		return;
	}

	/* try to read the package mask; if it doesn't exist assume solitary */
	snprintf(new_path, ADJ_SIZE(path, "/topology/core_siblings"),
		 "%s/topology/core_siblings", path);
	if (process_one_line(new_path, get_mask_from_bitmap, &package_mask)) {
		cpus_clear(package_mask);
		cpu_set(cpu->number, package_mask);
	}

	/* try to read the package id */
	snprintf(new_path, ADJ_SIZE(path, "/topology/physical_package_id"),
		 "%s/topology/physical_package_id", path);
	process_one_line(new_path, get_int, &packageid);

	/* try to read the cache mask; if it doesn't exist assume solitary */
	/* We want the deepest cache level available */
	max_cache_index = 0;
	cache_index = 1;
	do {
		struct stat sb;
		/* Extra 10 subtraction is for the max character length of %d */
		snprintf(new_path, ADJ_SIZE(path, "/cache/index%d/shared_cpu_map") - 10,
			 "%s/cache/index%d/shared_cpu_map", path, cache_index);
		cache_stat = stat(new_path, &sb);
		if (!cache_stat) {
			max_cache_index = cache_index;
			if (max_cache_index == deepest_cache)
				break;
			cache_index ++;
		}
	} while(!cache_stat);

	if (max_cache_index > 0) {
		/* Extra 10 subtraction is for the max character length of %d */
		snprintf(new_path, ADJ_SIZE(path, "/cache/index%d/shared_cpu_map") - 10,
			 "%s/cache/index%d/shared_cpu_map", path, max_cache_index);
		process_one_line(new_path, get_mask_from_bitmap, &cache_mask);
	}

	nodeid = NUMA_NO_NODE;
	if (numa_avail) {
		struct topo_obj *node;

		dir = opendir(path);
		while (dir) {
			entry = readdir(dir);
			if (!entry)
				break;
			if (strncmp(entry->d_name, "node", 4) == 0) {
				char *end;
				int num;
				num = strtol(entry->d_name + 4, &end, 10);
				if (!*end && num >= 0) {
					nodeid = num;
					break;
				}
			}
		}
		if (dir)
			closedir(dir);

		/*
		 * In case of multiple NUMA nodes within a CPU package,
		 * we override package_mask with node mask.
		 */
		node = get_numa_node(nodeid);
		if (node && (cpus_weight(package_mask) > cpus_weight(node->mask)))
			cpus_and(package_mask, package_mask, node->mask);
	}

	/*
	   blank out the banned cpus from the various masks so that interrupts
	   will never be told to go there
	 */
	cpus_and(cache_mask, cache_mask, unbanned_cpus);
	cpus_and(package_mask, package_mask, unbanned_cpus);

	cache = add_cpu_to_cache_domain(cpu, cache_mask, nodeid);
	if (cache)
		add_cache_domain_to_package(cache, packageid, package_mask, nodeid);

	cpu->obj_type_list = &cpus;
	cpus = g_list_append(cpus, cpu);
}

static void dump_irq(struct irq_info *info, void *data)
{
	int spaces = (long int)data;
	int i;
	char * indent = malloc (sizeof(char) * (spaces + 1));

	if (!indent)
		return;
	for ( i = 0; i < spaces; i++ )
		indent[i] = log_indent[0];

	indent[i] = '\0';
	log(TO_CONSOLE, LOG_INFO, "%sInterrupt %i node_num is %d (%s/%" PRIu64 ":%" PRIu64 ") \n", indent,
	    info->irq, irq_numa_node(info)->number, classes[info->class], info->load, (info->irq_count - info->last_irq_count));
	free(indent);
}

static void dump_numa_node_num(struct topo_obj *p, void *data __attribute__((unused)))
{
	log(TO_CONSOLE, LOG_INFO, "%d ", p->number);
}

static void dump_balance_obj(struct topo_obj *d, void *data __attribute__((unused)))
{
	struct topo_obj *c = (struct topo_obj *)d;
	log(TO_CONSOLE, LOG_INFO, "%s%s%s%sCPU number %i  numa_node is ",
	    log_indent, log_indent, log_indent, log_indent, c->number);
	for_each_object(cpu_numa_node(c), dump_numa_node_num, NULL);
	log(TO_CONSOLE, LOG_INFO, "(load %lu)\n", (unsigned long)c->load);
	if (c->interrupts)
		for_each_irq(c->interrupts, dump_irq, (void *)18);
}

static void dump_cache_domain(struct topo_obj *d, void *data)
{
	char *buffer = data;
	cpumask_scnprintf(buffer, 4095, d->mask);
	log(TO_CONSOLE, LOG_INFO, "%s%sCache domain %i:  numa_node is ",
	    log_indent, log_indent, d->number);
	for_each_object(d->numa_nodes, dump_numa_node_num, NULL);
	log(TO_CONSOLE, LOG_INFO, "cpu mask is %s  (load %lu) \n", buffer,
	    (unsigned long)d->load);
	if (d->children)
		for_each_object(d->children, dump_balance_obj, NULL);
	if (g_list_length(d->interrupts) > 0)
		for_each_irq(d->interrupts, dump_irq, (void *)10);
}

static void dump_package(struct topo_obj *d, void *data)
{
	char *buffer = data;
	cpumask_scnprintf(buffer, 4096, d->mask);
	log(TO_CONSOLE, LOG_INFO, "Package %i:  numa_node ", d->number);
	for_each_object(d->numa_nodes, dump_numa_node_num, NULL);
	log(TO_CONSOLE, LOG_INFO, "cpu mask is %s (load %lu)\n",
	    buffer, (unsigned long)d->load);
	if (d->children)
		for_each_object(d->children, dump_cache_domain, buffer);
	if (g_list_length(d->interrupts) > 0)
		for_each_irq(d->interrupts, dump_irq, (void *)2);
}

void dump_tree(void)
{
	char buffer[4096];
	for_each_object(packages, dump_package, buffer);
}

static void clear_irq_stats(struct irq_info *info, void *data __attribute__((unused)))
{
	info->load = 0;
}

static void clear_obj_stats(struct topo_obj *d, void *data __attribute__((unused)))
{
	for_each_object(d->children, clear_obj_stats, NULL);
	for_each_irq(d->interrupts, clear_irq_stats, NULL);
}

/*
 * this function removes previous state from the cpu tree, such as
 * which level does how much work and the actual lists of interrupts 
 * assigned to each component
 */
void clear_work_stats(void)
{
	for_each_object(numa_nodes, clear_obj_stats, NULL);
}


void parse_cpu_tree(void)
{
	DIR *dir;
	struct dirent *entry;

	setup_banned_cpus();

	cpus_complement(unbanned_cpus, banned_cpus);

	dir = opendir("/sys/devices/system/cpu");
	if (!dir)
		return;
	do {
		int num;
		char pad;
		entry = readdir(dir);
		/*
		 * We only want to count real cpus, not cpufreq and
		 * cpuidle
		 */
		if (entry &&
		    sscanf(entry->d_name, "cpu%d%c", &num, &pad) == 1 &&
		    !strchr(entry->d_name, ' ')) {
			char new_path[PATH_MAX];
			snprintf(new_path, PATH_MAX, "/sys/devices/system/cpu/%s", entry->d_name);
			do_one_cpu(new_path);
		}
	} while (entry);
	closedir(dir);
	for_each_object(packages, connect_cpu_mem_topo, NULL);

	if (debug_mode)
		dump_tree();

}

void free_cpu_topo(gpointer data)
{
	struct topo_obj *obj = data;

	g_list_free(obj->children);
	g_list_free(obj->interrupts);
	g_list_free(obj->numa_nodes);
	free(obj);
}

/*
 * This function frees all memory related to a cpu tree so that a new tree
 * can be read
 */
void clear_cpu_tree(void)
{
	g_list_free_full(packages, free_cpu_topo);
	packages = NULL;

	g_list_free_full(cache_domains, free_cpu_topo);
	cache_domains = NULL;
	cache_domain_count = 0;

	g_list_free_full(cpus, free_cpu_topo);
	cpus = NULL;
	cpus_clear(cpu_online_map);
}

static gint compare_cpus(gconstpointer a, gconstpointer b)
{
	const struct topo_obj *ai = a;
	const struct topo_obj *bi = b;

	return ai->number - bi->number;	
}

struct topo_obj *find_cpu_core(int cpunr)
{
	GList *entry;
	struct topo_obj find;

	find.number = cpunr;
	entry = g_list_find_custom(cpus, &find, compare_cpus);

	return entry ? entry->data : NULL;
}	

int get_cpu_count(void)
{
	return g_list_length(cpus);
}

