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
 * Due to NAPI, the actual number of interrupts for a network NIC is usually low
 * even though the amount of work is high; this file is there to compensate for this
 * by adding actual package counts to the calculated amount of work of interrupts
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/types.h>
#include <linux/ethtool.h>
#include <glib.h>
#include <net/if.h>
#include <linux/sockios.h>
#include <string.h>
#include <sys/ioctl.h>
#include <stdint.h>


#include "irqbalance.h"

struct nic {
	char ethname[64];
	int irq;
	uint64_t prev_pkt;
	int counter;
};

static GList *nics;


static int dev_to_irq(char *devname)
{
	int sock, ret;
	struct ifreq ifr;
	struct ethtool_value ethtool; 
	struct ethtool_drvinfo driver;
	FILE *file;
	char *line =  NULL;
	size_t size;
	int val;

	char buffer[PATH_MAX];

	memset(&ifr, 0, sizeof(struct ifreq));
	memset(&ethtool, 0, sizeof(struct ethtool_value));

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock<0) 
		return 0;

	strcpy(ifr.ifr_name, devname);

	driver.cmd = ETHTOOL_GDRVINFO;
        ifr.ifr_data = (void*) &driver;
        ret = ioctl(sock, SIOCETHTOOL, &ifr);
	close(sock);
	if (ret<0)
		return 0;
        sprintf(buffer,"/sys/bus/pci/devices/%s/irq", driver.bus_info);
	file = fopen(buffer, "r");
	if (!file)
		return 0;
	if (getline(&line, &size, file)==0) {
		free(line);
		fclose(file);
		return 0;
	}
	fclose(file);
	val = 0;
	if (line)
		val = strtoul(buffer, NULL, 10);
	free(line);
	return val;
}

static struct nic *new_nic(char *name)
{
	struct nic *nic;
	nic = malloc(sizeof(struct nic));
	if (!nic)
		return NULL;
	memset(nic, 0, sizeof(struct nic));
	strcpy(nic->ethname, name);
	nic->irq = dev_to_irq(name);
	nics = g_list_append(nics, nic);
	return nic;
}

static struct nic *find_nic(char *name)
{
	GList *item;
	struct nic *nic;
	item = g_list_first(nics);
	while (item) {
		nic = item->data;
		item = g_list_next(item);
		if (strcmp(nic->ethname, name)==0) {
			nic->counter++;
			/* refresh irq information once in a while; ifup/down
			 * can make this info go stale over time
			 */
			if ((nic->counter % NIC_REFRESH_INTERVAL) == 0)
				nic->irq = dev_to_irq(nic->ethname);
			return nic;
		}
	}
	nic = new_nic(name);
	return nic;
}

void account_for_nic_stats(void)
{
	struct nic *nic;
	FILE *file;
	char *line = NULL;
	size_t size = 0;
	file = fopen("/proc/net/dev", "r");
	if (!file)
		return;
	/* first two lines are headers */
	if (getline(&line, &size, file)==0) {
		free(line);
		return;
	}
	if (getline(&line, &size, file)==0) {
		free(line);
		return;
	}

	while (!feof(file)) {
		uint64_t rxcount;
		uint64_t txcount;
		uint64_t delta;
		int dummy;
		char *c, *c2;
		if (getline(&line, &size, file)==0)
			break;
		if (line==NULL)
			break;
		c = strchr(line, ':');
		if (c==NULL) /* header line */
			continue;
		*c = 0;
		c++;
		c2 = &line[0];
		while (*c2==' ') c2++;
		nic = find_nic(c2);
		if (!nic)
			continue;
		dummy = strtoul(c, &c, 10);
		rxcount = strtoull(c, &c, 10);
		dummy = strtoul(c, &c, 10);
		dummy = strtoul(c, &c, 10);
		dummy = strtoul(c, &c, 10);
		dummy = strtoul(c, &c, 10);
		dummy = strtoul(c, &c, 10);
		dummy = strtoul(c, &c, 10);
		dummy = strtoul(c, &c, 10);
		txcount = strtoull(c, &c, 10);
		delta = (txcount+rxcount-nic->prev_pkt)/2;
		/* add the RX and TX packets to the irq count, but only for 50%;
		   many packets generate another IRQ anyway and we don't want to
		   overweigh this too much */
		if (delta>0 && nic->prev_pkt != 0)
			add_interrupt_count(nic->irq, delta, IRQ_ETH);
		nic->prev_pkt = rxcount + txcount;
		
		
	}
	fclose(file);
	free(line);
}
