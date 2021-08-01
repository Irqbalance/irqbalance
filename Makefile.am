# Makefile.am --
# Copyright 2009 Red Hat Inc., Durham, North Carolina.
# All Rights Reserved.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#
# Authors:
#   Steve Grubb <sgrubb@redhat.com>
# 

AUTOMAKE_OPTIONS = no-dependencies
ACLOCAL_AMFLAGS = -I m4
EXTRA_DIST = COPYING autogen.sh misc/irqbalance.service misc/irqbalance.env

SUBDIRS = tests

UI_DIR = ui
AM_CFLAGS = $(LIBCAP_NG_CFLAGS) $(GLIB2_CFLAGS) $(NUMA_CFLAGS)
AM_CPPFLAGS = -I${top_srcdir} -W -Wall -Wshadow -Wformat -Wundef -D_GNU_SOURCE
noinst_HEADERS = bitmap.h constants.h cpumask.h irqbalance.h non-atomic.h \
	types.h $(UI_DIR)/helpers.h $(UI_DIR)/irqbalance-ui.h $(UI_DIR)/ui.h
sbin_PROGRAMS = irqbalance

if IRQBALANCEUI
sbin_PROGRAMS += irqbalance-ui
endif

irqbalance_SOURCES = activate.c bitmap.c classify.c cputree.c irqbalance.c \
	irqlist.c numa.c placement.c procinterrupts.c
irqbalance_LDADD = $(LIBCAP_NG_LIBS) $(GLIB2_LIBS) $(NUMA_LIBS)
if IRQBALANCEUI
irqbalance_ui_SOURCES = $(UI_DIR)/helpers.c $(UI_DIR)/irqbalance-ui.c \
	$(UI_DIR)/ui.c
irqbalance_ui_LDADD = $(GLIB2_LIBS) $(NUMA_LIBS) $(CURSES_LIBS)
endif

dist_man_MANS = irqbalance.1
if IRQBALANCEUI
dist_man_MANS += irqbalance-ui.1
endif

CONFIG_CLEAN_FILES = debug*.list config/*
clean-generic:
	rm -rf autom4te*.cache 
	rm -f *.rej *.orig *~

