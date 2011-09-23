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
#include "config.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>

#include "irqbalance.h"

void pci_numa_scan(void)
{
	int irq = -1;
	cpumask_t mask;
	int node_num;
	do {
		int type;
		irq = get_next_irq(irq);
		if (irq == -1)
			break;

		mask = find_irq_cpumask_prop(irq, IRQ_LCPU_MASK);

		node_num = find_irq_integer_prop(irq, IRQ_NUMA);

		type = find_irq_integer_prop(irq, IRQ_CLASS);

		add_interrupt_numa(irq, mask, node_num, type);
		
	} while (irq != -1);
}
