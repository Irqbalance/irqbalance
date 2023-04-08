/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <stdio.h>

#include <sys/sysinfo.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>

#include "irqbalance.h"

cpumask_t thermal_banned_cpus;

/* Events of thermal_genl_family */
enum thermal_genl_event {
	THERMAL_GENL_EVENT_UNSPEC,
	THERMAL_GENL_EVENT_TZ_CREATE,		/* Thermal zone creation */
	THERMAL_GENL_EVENT_TZ_DELETE,		/* Thermal zone deletion */
	THERMAL_GENL_EVENT_TZ_DISABLE,		/* Thermal zone disabled */
	THERMAL_GENL_EVENT_TZ_ENABLE,		/* Thermal zone enabled */
	THERMAL_GENL_EVENT_TZ_TRIP_UP,		/* Trip point crossed the way up */
	THERMAL_GENL_EVENT_TZ_TRIP_DOWN,	/* Trip point crossed the way down */
	THERMAL_GENL_EVENT_TZ_TRIP_CHANGE,	/* Trip point changed */
	THERMAL_GENL_EVENT_TZ_TRIP_ADD,		/* Trip point added */
	THERMAL_GENL_EVENT_TZ_TRIP_DELETE,	/* Trip point deleted */
	THERMAL_GENL_EVENT_CDEV_ADD,		/* Cdev bound to the thermal zone */
	THERMAL_GENL_EVENT_CDEV_DELETE,		/* Cdev unbound */
	THERMAL_GENL_EVENT_CDEV_STATE_UPDATE,	/* Cdev state updated */
	THERMAL_GENL_EVENT_TZ_GOV_CHANGE,	/* Governor policy changed  */
	THERMAL_GENL_EVENT_CAPACITY_CHANGE,	/* CPU capacity changed */
	__THERMAL_GENL_EVENT_MAX,
};
#define THERMAL_GENL_EVENT_MAX (__THERMAL_GENL_EVENT_MAX - 1)

/* Attributes of thermal_genl_family */
enum thermal_genl_attr {
	THERMAL_GENL_ATTR_UNSPEC,
	THERMAL_GENL_ATTR_TZ,
	THERMAL_GENL_ATTR_TZ_ID,
	THERMAL_GENL_ATTR_TZ_TEMP,
	THERMAL_GENL_ATTR_TZ_TRIP,
	THERMAL_GENL_ATTR_TZ_TRIP_ID,
	THERMAL_GENL_ATTR_TZ_TRIP_TYPE,
	THERMAL_GENL_ATTR_TZ_TRIP_TEMP,
	THERMAL_GENL_ATTR_TZ_TRIP_HYST,
	THERMAL_GENL_ATTR_TZ_MODE,
	THERMAL_GENL_ATTR_TZ_NAME,
	THERMAL_GENL_ATTR_TZ_CDEV_WEIGHT,
	THERMAL_GENL_ATTR_TZ_GOV,
	THERMAL_GENL_ATTR_TZ_GOV_NAME,
	THERMAL_GENL_ATTR_CDEV,
	THERMAL_GENL_ATTR_CDEV_ID,
	THERMAL_GENL_ATTR_CDEV_CUR_STATE,
	THERMAL_GENL_ATTR_CDEV_MAX_STATE,
	THERMAL_GENL_ATTR_CDEV_NAME,
	THERMAL_GENL_ATTR_GOV_NAME,
	THERMAL_GENL_ATTR_CAPACITY,
	THERMAL_GENL_ATTR_CAPACITY_CPU_COUNT,
	THERMAL_GENL_ATTR_CAPACITY_CPU_ID,
	THERMAL_GENL_ATTR_CAPACITY_CPU_PERF,
	THERMAL_GENL_ATTR_CAPACITY_CPU_EFF,
	__THERMAL_GENL_ATTR_MAX,
};
#define THERMAL_GENL_ATTR_MAX (__THERMAL_GENL_ATTR_MAX - 1)

#define INVALID_NL_FD		-1
#define MAX_RECV_ERRS		2
#define SYSCONF_ERR		-1
#define INVALID_EVENT_VALUE	-1

#define THERMAL_GENL_FAMILY_NAME	"thermal"
#define THERMAL_GENL_EVENT_GROUP_NAME	"event"
#define NL_FAMILY_NAME			"nlctrl"

struct family_data {
	const char *group;
	int id;
};

static struct nl_sock *sock;
static struct nl_cb *callback;

/*
 * return value: TRUE with an error; otherwise, FALSE
 */
static gboolean prepare_netlink(void)
{
	int rc;

	sock = nl_socket_alloc();
	if (!sock) {
		log(TO_ALL, LOG_ERR, "thermal: socket allocation failed.\n");
		return TRUE;
	}

	rc = genl_connect(sock);
	if (rc) {
		log(TO_ALL, LOG_INFO, "thermal: socket bind failed.\n");
		return TRUE;
	}

	callback = nl_cb_alloc(NL_CB_DEFAULT);
	if (!callback) {
		log(TO_ALL, LOG_WARNING, "thermal: callback allocation failed.\n");
		return TRUE;
	}

	return FALSE;
}

static int handle_groupid(struct nl_msg *msg, void *arg)
{
	struct nlattr *attrhdr, *mcgrp, *cur_mcgrp;
	struct nlattr *attrs[CTRL_ATTR_MAX + 1];
	struct nla_policy *policy = NULL;
	struct genlmsghdr *gnlhdr = NULL;
	struct family_data *data = NULL;
	struct nlmsghdr *msghdr = NULL;
	int attrlen, rc, i;

	if (!arg) {
		log(TO_ALL, LOG_WARNING, "thermal: group id - failed to receive argument.\n");
		return NL_SKIP;
	}
	data = arg;

	/* get actual netlink message header */
	msghdr = nlmsg_hdr(msg);

	/* get the start of the message payload */
	gnlhdr = nlmsg_data(msghdr);

	/* get the start of the message attribute section */
	attrhdr = genlmsg_attrdata(gnlhdr, 0);

	/* get the length of the message attribute section */
	attrlen = genlmsg_attrlen(gnlhdr, 0);

	/* create attribute index based on a stream of attributes */
	rc = nla_parse(
		attrs,		/* index array to be filled */
		CTRL_ATTR_MAX,	/* the maximum acceptable attribute type */
		attrhdr,	/* head of attribute stream */
		attrlen,	/* length of attribute stream */
		policy);	/* validation policy */
	if (rc) {
		log(TO_ALL, LOG_WARNING, "thermal: group id - failed to create attributes.\n");
		return NL_SKIP;
	}

	/* start of the multi-cast group attribute */
	mcgrp = attrs[CTRL_ATTR_MCAST_GROUPS];
	if (!mcgrp) {
		log(TO_ALL, LOG_WARNING, "thermal: group id - no multi-cast group attributes.\n");
		return NL_SKIP;
	}

	/* iterate a stream of nested attributes to get the group id */
	nla_for_each_nested(cur_mcgrp, mcgrp, i) {
		struct nlattr *nested_attrs[CTRL_ATTR_MCAST_GRP_MAX + 1];
		struct nlattr *name, *id;

		/* get start and length of payload section */
		attrhdr = nla_data(cur_mcgrp);
		attrlen = nla_len(cur_mcgrp);

		rc = nla_parse(nested_attrs, CTRL_ATTR_MCAST_GRP_MAX, attrhdr, attrlen, policy);
		if (rc)
			continue;

		name = nested_attrs[CTRL_ATTR_MCAST_GRP_NAME];
		id = nested_attrs[CTRL_ATTR_MCAST_GRP_ID];
		if (!name || !id)
			continue;

		if (strncmp(nla_data(name), data->group, nla_len(name)) != 0)
			continue;

		data->id = nla_get_u32(id);
		log(TO_ALL, LOG_DEBUG, "thermal: received group id (%d).\n", data->id);
		break;
	}
	return NL_OK;
}

static int handle_error(struct sockaddr_nl *sk_addr __attribute__((unused)),
			struct nlmsgerr *err, void *arg)
{
	int rc = (err->error == -NLE_INTR) ? NL_STOP : NL_SKIP;

	if (arg && err->error != -NLE_INTR) {
		log(TO_ALL, LOG_INFO, "thermal: received a netlink error (%s).\n",
		    nl_geterror(err->error));
		*((int *)arg) = err->error;
	}
	return rc;
}

static int handle_end(struct nl_msg *msg __attribute__((unused)), void *arg)
{
	if (arg)
		*((int *)arg) = 0;
	return NL_SKIP;
}

struct msgheader {
	unsigned char cmd, version;
	unsigned int port, seq;
	int id, hdrlen, flags;
};

static gboolean establish_netlink(void)
{
	struct msgheader msghdr = { CTRL_CMD_GETFAMILY, 0, 0, 0, 0, 0, 0 };
	struct family_data nldata = { THERMAL_GENL_EVENT_GROUP_NAME, -ENOENT };
	struct nl_cb *cloned_callback = NULL;
	int rc, group_id, callback_rc = 1;
	struct nl_msg *msg = NULL;
	gboolean error = TRUE;
	void *hdr;

	msg = nlmsg_alloc();
	if (!msg) {
		log(TO_ALL, LOG_WARNING, "thermal: message allocation failed.\n");
		goto err_out;
	}

	msghdr.id = genl_ctrl_resolve(sock, NL_FAMILY_NAME);
	if (msghdr.id < 0) {
		log(TO_ALL, LOG_WARNING, "thermal: message id enumeration failed.\n");
		goto err_out;
	}

	hdr = genlmsg_put(msg, msghdr.port, msghdr.seq, msghdr.id, msghdr.hdrlen,
			  msghdr.flags, msghdr.cmd, msghdr.version);
	if (!hdr) {
		log(TO_ALL, LOG_WARNING, "thermal: netlink header setup failed.\n");
		goto err_out;
	}

	rc = nla_put_string(msg, CTRL_ATTR_FAMILY_NAME, THERMAL_GENL_FAMILY_NAME);
	if (rc) {
		log(TO_ALL, LOG_WARNING, "thermal: message setup failed.\n");
		goto err_out;
	}

	cloned_callback = nl_cb_clone(callback);
	if (!cloned_callback) {
		log(TO_ALL, LOG_WARNING, "thermal: callback handle duplication failed.\n");
		goto err_out;
	}

	rc = nl_send_auto(sock, msg);
	if (rc < 0) {
		log(TO_ALL, LOG_WARNING, "thermal: failed to send the first message.\n");
		goto err_out;
	}

	rc = nl_cb_err(cloned_callback, NL_CB_CUSTOM, handle_error, &callback_rc);
	if (rc) {
		log(TO_ALL, LOG_WARNING, "thermal: error callback setup failed.\n");
		goto err_out;
	}

	rc = nl_cb_set(cloned_callback, NL_CB_ACK, NL_CB_CUSTOM, handle_end, &callback_rc);
	if (rc) {
		log(TO_ALL, LOG_WARNING, "thermal: ack callback setup failed.\n");
		goto err_out;
	}

	rc = nl_cb_set(cloned_callback, NL_CB_FINISH, NL_CB_CUSTOM, handle_end, &callback_rc);
	if (rc) {
		log(TO_ALL, LOG_WARNING, "thermal: finish callback setup failed.\n");
		goto err_out;
	}

	rc = nl_cb_set(cloned_callback, NL_CB_VALID, NL_CB_CUSTOM, handle_groupid, &nldata);
	if (rc) {
		log(TO_ALL, LOG_WARNING, "thermal: group id callback setup failed.\n");
		goto err_out;
	}

	while (callback_rc != 0) {
		rc = nl_recvmsgs(sock, cloned_callback);
		if (rc < 0) {
			log(TO_ALL, LOG_WARNING, "thermal: failed to receive messages.\n");
			goto err_out;
		}
	}

	group_id = nldata.id;
	if (group_id < 0) {
		log(TO_ALL, LOG_WARNING, "thermal: invalid group_id was received.\n");
		goto err_out;
	}

	rc = nl_socket_add_membership(sock, group_id);
	if (rc) {
		log(TO_ALL, LOG_WARNING, "thermal: failed to join the netlink group.\n");
		goto err_out;
	}

	error = FALSE;
err_out:
	nl_cb_put(cloned_callback);
	nlmsg_free(msg);
	return error;
}

enum {
	INDEX_CPUNUM,
	INDEX_PERF,
	INDEX_EFFI,
	INDEX_MAX
};

/*
 * Single netlink notification is not guaranteed to fully deliver all of
 * the CPU updates per thermal event, due to the implementation choice in
 * the kernel code. So, this function is intended to manage a CPU list in a
 * stream of relevant notifications.
 */
static void update_banned_cpus(int cur_cpuidx, gboolean need_to_ban)
{
	static cpumask_t banmask = { 0 }, itrmask = { 0 };
	long max_cpunum = sysconf(_SC_NPROCESSORS_ONLN);

	if (need_to_ban)
		cpu_set(cur_cpuidx, banmask);

	cpu_set(cur_cpuidx, itrmask);
	if (cpus_weight(itrmask) < max_cpunum)
		return;

	if (cpus_equal(thermal_banned_cpus, banmask))
		goto out;

	cpus_copy(thermal_banned_cpus, banmask);
	need_rescan = 1;
out:
	cpus_clear(banmask);
	cpus_clear(itrmask);
}

static int handle_thermal_event(struct nl_msg *msg, void *arg __attribute__((unused)))
{
	int event_data[INDEX_MAX] = { INVALID_EVENT_VALUE };
	struct nlattr *attrs[THERMAL_GENL_ATTR_MAX + 1];
	struct genlmsghdr *genlhdr;
	struct nlmsghdr *msnlh;
	struct nlattr *cap;
	int i, remain, rc;
	void *pos;

	/* get actual netlink message header */
	msnlh = nlmsg_hdr(msg);

	/* get a pointer to generic netlink header */
	genlhdr = genlmsg_hdr(msnlh);
	if (genlhdr->cmd != THERMAL_GENL_EVENT_CAPACITY_CHANGE) {
		log(TO_ALL, LOG_DEBUG, "thermal: no CPU capacity change.\n");
		return NL_SKIP;
	}

	/* parse generic netlink message including attributes */
	rc = genlmsg_parse(
		msnlh,			/* a pointer to netlink message header */
		0,			/* length of user header */
		attrs,			/* array to store parsed attributes */
		THERMAL_GENL_ATTR_MAX,	/* maximum attribute id as expected */
		NULL);			/* validation policy */
	if (rc) {
		log(TO_ALL, LOG_ERR, "thermal: failed to parse message for thermal event.\n");
		return NL_SKIP;
	}

	/* get start and length of payload section */
	cap = attrs[THERMAL_GENL_ATTR_CAPACITY];
	pos = nla_data(cap);
	remain = nla_len(cap);

	for (i = 0; nla_ok(pos, remain); pos = nla_next(pos, &remain), i++) {
		gboolean valid_event = TRUE, need_to_ban;
		unsigned int value = nla_get_u32(pos);
		int idx = i % INDEX_MAX;
		int cur_cpuidx;

		event_data[idx] = value;

		if (idx != INDEX_EFFI)
			continue;

		cur_cpuidx = event_data[INDEX_CPUNUM];
		valid_event = !!(cur_cpuidx <= NR_CPUS);
		if (!valid_event) {
			log(TO_ALL, LOG_WARNING, "thermal: invalid event - CPU %d\n",
			    cur_cpuidx);
			continue;
		}

		/*
		 * A CPU with no performance and no efficiency cannot
		 * handle IRQs:
		 */
		need_to_ban = !!(!event_data[INDEX_PERF] && !event_data[INDEX_EFFI]);
		update_banned_cpus(cur_cpuidx, need_to_ban);

		log(TO_ALL, LOG_DEBUG, "thermal: event - CPU %d, perf %d, efficiency %d.\n",
		    cur_cpuidx, event_data[INDEX_PERF], event_data[INDEX_EFFI]);
	}

	return NL_OK;
}

static int handler_for_debug(struct nl_msg *msg __attribute__((unused)),
			     void *arg __attribute__((unused)))
{
	return NL_OK;
}

/*
 * return value: TRUE with an error; otherwise, FALSE
 */
static gboolean register_netlink_handler(void)
{
	int rc;

	if (sysconf(_SC_NPROCESSORS_ONLN) == SYSCONF_ERR) {
		log(TO_ALL, LOG_WARNING, "thermal: _SC_NPROCESSORS_ONLN not available.\n");
		return TRUE;
	}

	rc = nl_cb_set(callback, NL_CB_SEQ_CHECK, NL_CB_CUSTOM, handler_for_debug, NULL);
	if (rc) {
		log(TO_ALL, LOG_WARNING, "thermal: debug handler registration failed.\n");
		return TRUE;
	}


	rc = nl_cb_set(callback, NL_CB_VALID, NL_CB_CUSTOM, handle_thermal_event, NULL);
	if (rc) {
		log(TO_ALL, LOG_WARNING, "thermal: thermal handler registration failed.\n");
		return TRUE;
	}

	return FALSE;
}

/*
 * return value: TRUE to keep the source; FALSE to disconnect.
 */
gboolean receive_thermal_event(gint fd __attribute__((unused)),
			       GIOCondition condition,
			       gpointer user_data __attribute__((unused)))
{
	if (condition == G_IO_IN) {
		static unsigned int retry = 0;
		int err;

		err = nl_recvmsgs(sock, callback);
		if (err) {
			log(TO_ALL, LOG_WARNING, "thermal: failed to receive messages (rc=%d).\n", err);
			retry++;

			/*
			 * Pass a few failures then turn off if it keeps
			 * failing down.
			 */
			if (retry <= MAX_RECV_ERRS) {
				log(TO_ALL, LOG_WARNING, "thermal: but keep the connection.\n");
			} else {
				log(TO_ALL, LOG_WARNING, "thermal: disconnect now with %u failures.\n",
				    retry);
				return FALSE;
			}
		}
	}
	return TRUE;
}

/*
 * return value: TRUE with an error; otherwise, FALSE
 */
static gboolean set_netlink_nonblocking(void)
{
	int rc, fd;

	rc = nl_socket_set_nonblocking(sock);
	if (rc) {
		log(TO_ALL, LOG_WARNING, "thermal: non-blocking mode setup failed.\n");
		return TRUE;
	}

	fd = nl_socket_get_fd(sock);
	if (fd == INVALID_NL_FD) {
		log(TO_ALL, LOG_WARNING, "thermal: file descriptor setup failed.\n");
		return TRUE;
	}

	g_unix_fd_add(fd, G_IO_IN, receive_thermal_event, NULL);

	return FALSE;
}

void deinit_thermal(void)
{
	if (callback) {
		nl_cb_put(callback);
		callback = NULL;
	}
	if (sock) {
		nl_socket_free(sock);
		sock = NULL;
	}
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

	error = register_netlink_handler();
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
