#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "irqbalance.h"
#include "types.h"


char *classes[] = {
	"other",
	"legacy",
	"storage",
	"timer",
	"ethernet",
	"fasteth",
	0
};

int map_class_to_level[7] = 
{ BALANCE_PACKAGE, BALANCE_CACHE, BALANCE_CACHE, BALANCE_NONE, BALANCE_CORE, BALANCE_CORE };


int class_counts[7];

/*

NOTE NOTE although that this file has a hard-coded list of modules, something missing is not
          a big deal; the types are also set based on PCI class information when available.

*/

/*

   Based on the original irqbalance code which is:

   Copyright (C) 2003 Red Hat, Inc. All rights reserved.
                                                                                                             
   Usage and distribution of this file are subject to the Gnu General Public License Version 2 
   that can be found at http://www.gnu.org/licenses/gpl.txt and the COPYING file as
   distributed together with this file is included herein by reference.  
                                                                                                                      
   Author: Arjan van de Ven   <arjanv@redhat.com>

*/

static char *legacy_modules[] = {
 	"PS/2",
 	"serial",
 	"i8042",
 	"acpi",
	"floppy",
	"parport",
 	"keyboard",
 	"usb-ohci",
	"usb-uhci",
	"uhci_hcd",
	"ohci_hcd",
	"ehci_hcd",
	"EMU10K1",
	0
};

static char *timer_modules[] = {
 	"rtc",
 	"timer",
	0
};

static char *storage_modules[] = {
	"aic7xxx",
	"aic79xx",
	"ide",
	"cciss",
	"cpqarray",
	"qla2",
	"megaraid",
	"fusion",
	"libata",
	"ohci1394",
	"sym53c8xx",
	0
};

static char *ethernet_modules[] = {
	"eth",
	"e100",
	"eepro100",
	"orinico_cs",
	"wvlan_cs",
	"3c5",	
	"HiSax",
	0
};


int find_class(struct interrupt *irq, char *moduletext)
{
	int guess = IRQ_OTHER;
	int i;
	
	if (moduletext == NULL)
		return guess;
		
	for (i=0; legacy_modules[i]; i++)
		if (strstr(moduletext, legacy_modules[i]))
			guess = IRQ_LEGACY;

	for (i=0; storage_modules[i]; i++)
		if (strstr(moduletext, storage_modules[i]))
			guess = IRQ_SCSI;

	for (i=0; timer_modules[i]; i++)
		if (strstr(moduletext, timer_modules[i]))
			guess = IRQ_TIMER;
			
	for (i=0; ethernet_modules[i]; i++)
		if (strstr(moduletext, ethernet_modules[i]))
			guess = IRQ_ETH;

	if (guess == IRQ_OTHER && irq->number==0)
		guess = IRQ_TIMER;
	
	if (guess >  irq->class)	
		return guess;
        return irq->class;
}
