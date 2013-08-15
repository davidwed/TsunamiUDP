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

AM_CPPFLAGS		= -I$(top_srcdir)/include

noinst_LIBRARIES		= libtsunami_common.a
libtsunami_common_a_SOURCES= md5.c common.c error.c

# Uncomment this on Playstation3 or other big endian platforms
# before running 'configure':
# AM_CFLAGS = -DARCH_IS_BIG_ENDIAN 
