#ifndef __LINUX_THERMAL_H_
#define __LINUX_THERMAL_H_

#include <glib.h>

#ifdef HAVE_THERMAL
gboolean init_thermal(void);
void deinit_thermal(void);
extern cpumask_t thermal_banned_cpus;
#else
static inline gboolean init_thermal(void) { return FALSE; }
#define deinit_thermal() do { } while (0)
#endif

#endif /* __LINUX_THERMAL_H_ */
