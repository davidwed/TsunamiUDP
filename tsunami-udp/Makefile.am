# -*- makefile -*-
#
# Copyright (c) 2001-2002 The Trustees of Indiana University.  
#                         All rights reserved.
# 
# This file is part of the Tsunami software package.  For license
# information, see the LICENSE file in the top level directory of the
# Tsunami source distribution.
#
# $Id$
#

SUBDIRS			= include common client server util rtserver rtclient
# SUBDIRS			= include common client server util rtserver rtclient mk5server

# For some reason, we have to list depcomp explicitly, even though
# it's an automake-generated file.  :-(

EXTRA_DIST = \
	LICENSE.txt \
	README.txt \
	depcomp \
	tsunami.spec.in

# another ugly but needed hack for RPM generation

dist-hook: tsunami.spec
	cp tsunami.spec $(distdir)
