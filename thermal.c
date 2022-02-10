/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <stdio.h>

#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>

#include "irqbalance.h"

cpumask_t thermal_banned_cpus;

static gboolean prepare_netlink(void)
{
	gboolean error = TRUE;

	log(TO_ALL, LOG_ERR, "thermal: not yet implement to alloc memory for netlink.\n");
	return error;
}

#define NL_FAMILY_NAME	"nlctrl"

static gboolean establish_netlink(void)
{
	gboolean error = TRUE;

	log(TO_ALL, LOG_ERR, "thermal: not yet implemented to establish netlink.\n");
	return error;
}

static gboolean register_netlink_handler(nl_recvmsg_msg_cb_t handler __attribute__((unused)))
{
	gboolean error = TRUE;

	log(TO_ALL, LOG_ERR, "thermal: not yet implemented to register thermal handler.\n");
	return error;
}

static gboolean set_netlink_nonblocking(void)
{
	gboolean error = TRUE;

	log(TO_ALL, LOG_ERR, "thermal: not yet implemented to set nonblocking socket.\n");
	return error;
}

void deinit_thermal(void)
{
	return;
}

/*
 * return value: TRUE with an error; otherwise, FALSE
 */
gboolean init_thermal(void)
{
	gboolean error;

	error = prepare_netlink();
	if (error)
		goto err_out;

	error = establish_netlink();
	if (error)
		goto err_out;

	error = register_netlink_handler(NULL);
	if (error)
		goto err_out;

	error = set_netlink_nonblocking();
	if (error)
		goto err_out;

	return FALSE;
err_out:
	deinit_thermal();
	return TRUE;
}
