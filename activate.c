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
 * This file contains the code to communicate a selected distribution / mapping
 * of interrupts to the kernel.
 */
#include "config.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#include "irqbalance.h"

/*
 * Maximum number of fallback attempts per IRQ per cycle to prevent
 * infinite loops when all CPUs are saturated.
 *
 * The value 8 is based on typical x86 CPU cache topology:
 * - Modern CPUs have 2-8 physical cores sharing an L3 cache domain
 * - With SMT/Hyperthreading, this becomes 4-16 logical CPUs per domain
 * - 8 attempts is sufficient to exhaust a typical cache domain before
 *   escalating to NUMA or global scope fallback
 *
 * On systems with more than 8 cores:
 * - If a CPU has slots_left <= 0, it is skipped (not counted as attempt)
 * - The loop exits early via "no fallback CPU available" when all
 *   valid candidates are exhausted, before hitting this limit
 * - Only worst-case gradual saturation (each CPU returns ENOSPC only
 *   when tried) would hit this limit, which is rare in practice
 *
 * Trade-off: Higher values could cover more CPUs but risk stalling
 * IRQ activation for other interrupts. Deferred IRQs are retried
 * on the next rebalance cycle (default 10 seconds).
 */
#define MAX_FALLBACK_ATTEMPTS 8

/*
 * Minimum slots_left value to track saturation without unbounded negative drift.
 * When a CPU is saturated, slots_left is set to this value to indicate
 * "full, don't use" without going arbitrarily negative.
 */
#define SLOTS_SATURATED 0

/*
 * Check if a CPU is a valid fallback candidate.
 * Returns 1 if valid, 0 otherwise.
 *
 * FIX: Added NULL check for cpu parameter to prevent NULL dereference
 * when GList element has NULL data.
 */
static int is_valid_fallback(struct topo_obj *cpu, struct topo_obj *original,
			     cpumask_t *tried_cpus)
{
	/* FIX: NULL dereference protection */
	if (!cpu)
		return 0;
	if (cpu == original || cpu->obj_type != OBJ_TYPE_CPU)
		return 0;
	/* FIX: Use <= 0 check for slots_left saturation */
	if (cpu->slots_left <= SLOTS_SATURATED)
		return 0;
	if (!cpus_intersects(cpu->mask, unbanned_cpus))
		return 0;
	if (cpus_intersects(cpu->mask, *tried_cpus))
		return 0;  /* Already tried this CPU */
	return 1;
}

/*
 * Recursively collect all CPU objects under a given topology object.
 * This fixes the NUMA fallback issue where numa_node->children contains
 * packages/caches, not CPUs directly.
 *
 * FIX: Traverse topology tree to find actual CPU objects within NUMA node.
 */
static struct topo_obj *find_best_cpu_under_obj(struct topo_obj *obj,
						struct topo_obj *original,
						cpumask_t *tried_cpus)
{
	struct topo_obj *best = NULL;
	GList *iter;

	if (!obj)
		return NULL;

	/* If this is a CPU, check if it's a valid fallback */
	if (obj->obj_type == OBJ_TYPE_CPU) {
		if (is_valid_fallback(obj, original, tried_cpus))
			return obj;
		return NULL;
	}

	/* Otherwise, recursively search children */
	for (iter = obj->children; iter; iter = iter->next) {
		struct topo_obj *child = iter->data;
		struct topo_obj *candidate;

		/* FIX: NULL check for child */
		if (!child)
			continue;

		candidate = find_best_cpu_under_obj(child, original, tried_cpus);
		if (candidate) {
			/* FIX: Select CPU with most slots_left */
			if (!best || candidate->slots_left > best->slots_left)
				best = candidate;
		}
	}
	return best;
}

/*
 * Convert balance level to string for logging.
 */
static const char *balance_level_str(int level)
{
	switch (level) {
	case BALANCE_NONE:
		return "none";
	case BALANCE_PACKAGE:
		return "package";
	case BALANCE_CACHE:
		return "cache";
	case BALANCE_CORE:
		return "core";
	default:
		return "unknown";
	}
}

/*
 * Try to find an alternative CPU when the primary target returns ENOSPC.
 * Returns 0 on success (IRQ placed on alternative CPU), -1 on failure.
 *
 * Uses iteration with a tried_cpus bitmask to avoid retrying the same CPU.
 *
 * IMPORTANT: This function respects the IRQ's configured balance level.
 * If the user configured an IRQ to balance at a specific level (e.g., cache),
 * fallback will only search within that scope. If no CPU is available within
 * the constrained scope, we warn the user and fail rather than violating the
 * configured policy.
 *
 * Scope by balance level:
 *   - BALANCE_CORE: No fallback possible (single CPU, fail immediately)
 *   - BALANCE_CACHE: Only search within same cache domain
 *   - BALANCE_PACKAGE: Only search within same package
 *   - BALANCE_NONE: User opted out of balancing - decline fallback
 *
 * FIXES APPLIED:
 *   1. NUMA fallback now traverses topology tree to find actual CPUs
 *   2. NULL dereference protection throughout
 *   3. Use snprintf() instead of sprintf() for path construction
 *   4. Capture errno immediately after failing syscall
 *   5. Consistent slots_left update logic (saturate at 0, not negative)
 *   6. Guard slots_left decrement on success
 *   7. Load-aware CPU selection (most slots_left first to avoid bias)
 *   8. Respect user-configured balance level policy
 */
static int try_fallback_cpu(struct irq_info *info, cpumask_t applied_mask,
			    int attempt __attribute__((unused)))
{
	struct topo_obj *original = info->assigned_obj;
	struct topo_obj *fallback = NULL;
	struct topo_obj *search_scope = NULL;
	cpumask_t tried_cpus;
	char buf[PATH_MAX];
	FILE *file;
	int ret;
	int saved_errno;
	int attempts = 0;
	int balance_level = info->level;

	/*
	 * BALANCE_CORE means the IRQ is pinned to a specific CPU.
	 * No fallback is possible without violating the configured policy.
	 */
	if (balance_level == BALANCE_CORE) {
		log(TO_ALL, LOG_WARNING,
			"IRQ %d: cannot fallback - balance level is 'core' "
			"(CPU %d saturated, no alternative within policy)\n",
			info->irq, original->number);
		return -1;
	}

	/*
	 * BALANCE_NONE means the user opted this IRQ out of balancing.
	 * place_irq_in_node() only relocates such an IRQ when banned CPUs
	 * forced it; we should not silently widen the scope further on
	 * ENOSPC. Treat it like BALANCE_CORE: warn and decline.
	 * This also avoids a NULL search_scope on non-NUMA systems where
	 * no OBJ_TYPE_NODE ancestor exists in the topology.
	 */
	if (balance_level == BALANCE_NONE) {
		log(TO_ALL, LOG_WARNING,
			"IRQ %d: cannot fallback - balance level is 'none' "
			"(user policy forbids relocation)\n",
			info->irq);
		return -1;
	}

	cpus_clear(tried_cpus);

	/*
	 * Determine the search scope based on assigned_obj type.
	 *
	 * For CPU-assigned IRQs: search within the appropriate parent domain
	 * based on balance_level (cache → package → NUMA).
	 *
	 * For domain-assigned IRQs: the assigned domain IS the search scope.
	 * This respects the user's balance_level policy - we only search
	 * within the domain irqbalance already chose for this IRQ.
	 */
	if (original->obj_type == OBJ_TYPE_CPU) {
		/* CPU-assigned: find appropriate parent based on balance_level */
		switch (balance_level) {
		case BALANCE_CACHE:
			search_scope = original->parent;
			while (search_scope && search_scope->obj_type != OBJ_TYPE_CACHE)
				search_scope = search_scope->parent;
			break;
		case BALANCE_PACKAGE:
			search_scope = original->parent;
			while (search_scope && search_scope->obj_type != OBJ_TYPE_PACKAGE)
				search_scope = search_scope->parent;
			break;
		default:
			search_scope = NULL;
		}
	} else {
		/*
		 * Domain-assigned IRQ: the assigned object IS the search scope.
		 * We search for individual CPUs within this domain.
		 */
		search_scope = original;
		log(TO_ALL, LOG_DEBUG,
			"IRQ %d: domain-assigned (obj_type=%d), searching within assigned scope\n",
			info->irq, original->obj_type);
	}

	if (!search_scope) {
		log(TO_ALL, LOG_WARNING,
			"IRQ %d: no valid search scope for fallback (balance_level=%s)\n",
			info->irq, balance_level_str(balance_level));
		return -1;
	}

	while (attempts < MAX_FALLBACK_ATTEMPTS) {
		/*
		 * Search for fallback CPU within the determined scope.
		 * Use recursive traversal to find actual CPU objects.
		 *
		 * For CPU-assigned IRQs: 'original' is excluded from candidates.
		 * For domain-assigned IRQs: 'original' is not a CPU, so
		 * is_valid_fallback() will skip it automatically.
		 */
		fallback = find_best_cpu_under_obj(search_scope, original, &tried_cpus);

		if (!fallback) {
			log(TO_ALL, LOG_WARNING,
				"IRQ %d: no fallback CPU available within "
				"scope (balance_level=%s, obj_type=%d)\n",
				info->irq, balance_level_str(balance_level),
				search_scope->obj_type);
			return -1;
		}

		/* Mark this CPU as tried */
		cpus_or(tried_cpus, tried_cpus, fallback->mask);
		attempts++;

		if (original->obj_type == OBJ_TYPE_CPU) {
			log(TO_ALL, LOG_DEBUG,
				"IRQ %d: ENOSPC fallback from CPU %d to CPU %d "
				"(attempt %d, slots_left=%d)\n",
				info->irq, original->number, fallback->number,
				attempts, fallback->slots_left);
		} else {
			log(TO_ALL, LOG_DEBUG,
				"IRQ %d: ENOSPC fallback to CPU %d "
				"(attempt %d, slots_left=%d, scope_type=%d)\n",
				info->irq, fallback->number,
				attempts, fallback->slots_left, original->obj_type);
		}

		/* Update assignment and compute new mask */
		info->assigned_obj = fallback;
		cpus_and(applied_mask, cpu_online_map, fallback->mask);

		/* FIX: Use snprintf() to prevent buffer overflow */
		ret = snprintf(buf, sizeof(buf), "/proc/irq/%i/smp_affinity",
			info->irq);
		if (ret < 0 || ret >= (int)sizeof(buf)) {
			log(TO_ALL, LOG_WARNING,
				"IRQ %d: path buffer overflow\n", info->irq);
			info->assigned_obj = original;
			continue;
		}

		file = fopen(buf, "w");
		if (!file) {
			/* FIX: Capture errno immediately */
			saved_errno = errno;
			log(TO_ALL, LOG_DEBUG,
				"IRQ %d: cannot open %s: %s\n",
				info->irq, buf, strerror(saved_errno));
			info->assigned_obj = original;
			continue;  /* Try next CPU */
		}

		cpumask_scnprintf(buf, PATH_MAX, applied_mask);
		ret = fprintf(file, "%s", buf);
		/* FIX: Capture errno immediately after potential failure */
		saved_errno = errno;
		if (ret >= 0) {
			if (fflush(file)) {
				ret = -1;
				saved_errno = errno;
			}
		}
		fclose(file);

		if (ret < 0) {
			/*
			 * FIX: Consistent slots_left update logic.
			 * On ENOSPC, mark CPU as saturated (slots_left = 0)
			 * instead of arbitrary negative values.
			 */
			if (saved_errno == ENOSPC) {
				fallback->slots_left = SLOTS_SATURATED;
				log(TO_ALL, LOG_DEBUG,
					"IRQ %d: fallback CPU %d saturated "
					"(ENOSPC), slots_left set to %d\n",
					info->irq, fallback->number,
					SLOTS_SATURATED);
			}
			info->assigned_obj = original;
			continue;  /* Try next CPU */
		}

		/*
		 * Success! Properly migrate the IRQ from original to fallback.
		 * FIX: Use migrate_irq_obj() to:
		 *   1. Move IRQ from original->interrupts to fallback->interrupts
		 *   2. Update slots_left on both CPUs
		 *   3. Update load on fallback CPU
		 *   4. Set info->assigned_obj = fallback
		 *
		 */
		migrate_irq_obj(original, fallback, info);
		/*
		 * migrate_irq_obj() unconditionally increments the source
		 * object's slots_left (0 -> 1), which would make the original
		 * CPU look eligible again. The kernel returned ENOSPC, so its
		 * vector table is still full; re-clamp to SATURATED so we
		 * don't immediately retry the same dead-end placement.
		 *
		 * Only re-clamp when the source is an actual CPU. For
		 * domain-assigned IRQs (cache/package/NUMA), the original
		 * is a domain object whose slots_left aggregates its CPUs
		 * and was never marked SLOTS_SATURATED by the ENOSPC handler
		 * (see activate_mapping(): the obj_type==CPU branch).
		 * Force-saturating the whole domain would penalize every CPU
		 * inside it on the next placement cycle.
		 */
		if (original->obj_type == OBJ_TYPE_CPU)
			original->slots_left = SLOTS_SATURATED;
		info->moved = 0;
		log(TO_ALL, LOG_DEBUG,
			"IRQ %d: successfully placed on fallback CPU %d "
			"(slots_left now %d)\n",
			info->irq, fallback->number, fallback->slots_left);
		return 0;
	}

	log(TO_ALL, LOG_WARNING,
		"IRQ %d: max fallback attempts (%d) reached, "
		"all CPUs saturated within policy scope\n",
		info->irq, MAX_FALLBACK_ATTEMPTS);
	return -1;
}

static int check_affinity(struct irq_info *info, cpumask_t applied_mask)
{
	cpumask_t current_mask;
	char buf[PATH_MAX];

	sprintf(buf, "/proc/irq/%i/smp_affinity", info->irq);
	if (process_one_line(buf, get_mask_from_bitmap, &current_mask) < 0)
		return 1;

	return cpus_equal(applied_mask, current_mask);
}

static void activate_mapping(struct irq_info *info, void *data __attribute__((unused)))
{
	char buf[PATH_MAX];
	FILE *file;
	int errsave, ret;
	cpumask_t applied_mask;

	/*
 	 * only activate mappings for irqs that have moved
 	 */
	if (!info->moved)
		return;

	if (!info->assigned_obj)
		return;

	/* activate only online cpus, otherwise writing to procfs returns EOVERFLOW */
	cpus_and(applied_mask, cpu_online_map, info->assigned_obj->mask);

	/*
 	 * Don't activate anything for which we have an invalid mask 
 	 */
	if (check_affinity(info, applied_mask)) {
		info->moved = 0; /* nothing to do, mark as done */
		return;
	}

	sprintf(buf, "/proc/irq/%i/smp_affinity", info->irq);
	file = fopen(buf, "w");
	errsave = errno;
	if (!file)
		goto error;

	cpumask_scnprintf(buf, PATH_MAX, applied_mask);
	ret = fprintf(file, "%s", buf);
	errsave = errno;
	if (ret >= 0 && fflush(file)) {
		ret = -1;
		errsave = errno;
	}
	if (fclose(file)) {
		ret = -1;
		errsave = errno;
	}
	if (ret < 0)
		goto error;
	info->moved = 0; /*migration is done*/
	return;
error:
	/* Use EPERM as the explaination for EIO */
	errsave = (errsave == EIO) ? EPERM : errsave;
	log(TO_ALL, LOG_DEBUG,
		"Cannot change IRQ %i affinity: %s\n",
		info->irq, strerror(errsave));
	switch (errsave) {
	case EAGAIN: /* Interrupted by signal. */
	case EBUSY: /* Affinity change already in progress. */
	case EINVAL: /* IRQ would be bound to no CPU. */
	case ERANGE: /* CPU in mask is offline. */
	case ENOMEM: /* Kernel cannot allocate CPU mask. */
		/* Do not blacklist the IRQ on transient errors. */
		break;
	case ENOSPC: /* Specified CPU APIC is full. */
		/*
		 * For CPU-assigned IRQs, mark the CPU as saturated.
		 * For domain-assigned IRQs (cache/package/NUMA), we cannot
		 * determine which specific CPU failed, so skip slots_left update.
		 */
		if (info->assigned_obj->obj_type == OBJ_TYPE_CPU) {
			info->assigned_obj->slots_left = SLOTS_SATURATED;
			log(TO_ALL, LOG_DEBUG,
				"IRQ %d: CPU %d saturated (ENOSPC), slots_left set to %d\n",
				info->irq, info->assigned_obj->number, SLOTS_SATURATED);
		} else {
			log(TO_ALL, LOG_DEBUG,
				"IRQ %d: ENOSPC on domain-assigned IRQ (obj_type=%d)\n",
				info->irq, info->assigned_obj->obj_type);
		}

		/*
		 * Try fallback CPUs before giving up. This allows IRQs to
		 * be redistributed in the same cycle rather than waiting
		 * for the next rebalance iteration.
		 */
		if (try_fallback_cpu(info, applied_mask, 0) == 0)
			break; /* Success - IRQ placed on alternative CPU */

		/*
		 * All fallback attempts failed. Mark for reconsideration
		 * in next cycle by resetting moved flag and forcing rebalance.
		 */

		force_rebalance_irq(info, NULL);
		info->moved = 0; /* Allow reconsideration in next cycle */
		break;
	default:
		/* Any other error is considered permanent. */
		info->level = BALANCE_NONE;
		info->moved = 0; /* migration impossible, mark as done */
		log(TO_ALL, LOG_DEBUG, "IRQ %i affinity is now unmanaged\n",
			info->irq);
	}
}

void activate_mappings(void)
{
	for_each_irq(NULL, activate_mapping, NULL);
}
