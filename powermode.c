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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#include "irqbalance.h"


extern int power_mode;

static uint64_t previous;

static unsigned int hysteresis;

void check_power_mode(void)
{
	FILE *file;
	char line[4096];
	char *c;
	uint64_t dummy, irq, softirq;
	line[0]=0;
	line[4095]=0;
	file = fopen("/proc/stat", "r");
	if (!file)
		return;
	if (fgets(line, 4095, file)==NULL)
		memset(line,0, 4096);
	fclose(file);
	c=&line[4];
	dummy = strtoull(c, &c, 10); /* user */
	dummy = strtoull(c, &c, 10); /* nice */
	dummy = strtoull(c, &c, 10); /* system */
	dummy = strtoull(c, &c, 10); /* idle */
	dummy = strtoull(c, &c, 10); /* iowait */
	irq = strtoull(c, &c, 10); /* irq */
	softirq = strtoull(c, &c, 10); /* softirq */

	irq += softirq;
	if (irq - previous <  POWER_MODE_SOFTIRQ_THRESHOLD)  {
		hysteresis++;
		if (hysteresis > POWER_MODE_HYSTERESIS) {
			if (debug_mode && !power_mode)
				printf("IRQ delta is %lu, switching to power mode \n", (unsigned long)(irq - previous) );
			power_mode = 1;
		}
	} else {
		if (debug_mode && power_mode)
			printf("IRQ delta is %lu, switching to performance mode \n", (unsigned long)(irq - previous) );
		power_mode = 0;
		hysteresis = 0;
	}
	previous = irq;
}

