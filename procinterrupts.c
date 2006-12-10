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
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#include "cpumask.h"
#include "irqbalance.h"

#define LINESIZE 4096

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
		return;
	}

	while (!feof(file)) {
		cpumask_t present;
		int cpunr;
		int	 number;
		uint64_t count;
		char *c, *c2;

		if (getline(&line, &size, file)==0)
			break;


		/* lines with letters in front are special, like NMI count. Ignore */
		if (!(line[0]==' ' || (line[0]>='0' && line[0]<='9')))
			break;
		c = strchr(line, ':');
		if (!c)
			continue;
		*c = 0;
		c++;
		number = strtoul(line, NULL, 10);
		cpus_clear(present);
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
			if (C)
				cpu_set(cpunr, present);
			cpunr++;
		}
		if (cpunr != core_count) 
			need_cpu_rescan = 1;
		
		set_interrupt_count(number, count, &present);
	}		
	fclose(file);
	free(line);
}
