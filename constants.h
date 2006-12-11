#ifndef __INCLUDE_GUARD_CONSTANTS_H
#define __INCLUDE_GUARD_CONSTANTS_H

/* interval between rebalance attempts in seconds */
#define SLEEP_INTERVAL 10

/* NUMA topology refresh intervals, in units of SLEEP_INTERVAL */
#define NUMA_REFRESH_INTERVAL 32
/* NIC interrupt refresh interval, in units of SLEEP_INTERVAL */
#define NIC_REFRESH_INTERVAL 32

/* minimum number of interrupts since boot for an interrupt to matter */
#define MIN_IRQ_COUNT	20


/* balancing tunings */

#define CROSS_PACKAGE_PENALTY		3000
#define NUMA_PENALTY			250
#define POWER_MODE_PACKAGE_THRESHOLD 	20000
#define CLASS_VIOLATION_PENTALTY	6000
#define CORE_SPECIFIC_THRESHOLD		5000

/* power mode */

#define POWER_MODE_SOFTIRQ_THRESHOLD	20
#define POWER_MODE_HYSTERESIS		3


#endif
