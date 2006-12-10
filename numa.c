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
 * This file tries to map numa affinity of pci devices to their interrupts
 * In addition the PCI class information is used to refine the classification
 * of interrupt sources 
 */
#define _GNU_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>

#include "irqbalance.h"

void pci_numa_scan(void)
{
	DIR *dir;
	struct dirent *entry;
	cpumask_t mask;
	char line[PATH_MAX];
	FILE *file;
	int irq;
	unsigned int class;

	dir = opendir("/sys/bus/pci/devices");
	if (!dir) 
		return;
	do {
		int type;
		entry = readdir(dir);
		if (!entry)
			break;
		if (strlen(entry->d_name)<3)
			continue;

		sprintf(line,"/sys/bus/pci/devices/%s/irq", entry->d_name);
		file = fopen(line, "r");
		if (!file)
			continue;
		if (fgets(line, PATH_MAX, file)==NULL)
			line[0]=0;
		fclose(file);
		irq = strtoul(line, NULL, 10);
		if (!irq)
			continue;

		sprintf(line,"/sys/bus/pci/devices/%s/class", entry->d_name);
		file = fopen(line, "r");
		if (!file)
			continue;
		if (fgets(line, PATH_MAX, file)==NULL)
			line[0]=0;
		fclose(file);
		class = strtoul(line, NULL, 16);

		sprintf(line,"/sys/bus/pci/devices/%s/local_cpus", entry->d_name);
		file = fopen(line, "r");
		if (!file)
			continue;
		if (fgets(line, PATH_MAX, file)==NULL)
			line[0]=0;
		fclose(file);
		cpumask_parse_user(line, strlen(line), mask);

		type = IRQ_OTHER;
		if ((class>>16) == 0x01)
			type = IRQ_SCSI;
/*
 * Ethernet gets the type via /proc/net/dev; in addition down'd interfaces
 * shouldn't boost interrupts 
		if ((class>>16) == 0x02)
			type = IRQ_ETH;
*/
		if ((class>>16) >= 0x03 && (class>>16) <= 0x0C)
			type = IRQ_LEGACY;

		add_interrupt_numa(irq, mask, type);
		
	} while (entry);
	closedir(dir);	
}
