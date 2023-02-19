/**
 * NLINKFS - A File System that handles symbolic links transparently
 *
 * Copyright (C)2010 Renê de Souza Pinto (rene at renesp.com.br)
 *
 * This file is part of NLINKFS, NLINKFS is a File System.
 *
 * NLINKFS is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2.
 *
 * NLINKFS is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Bash; see the file COPYING.  If not, write to the Free
 * Software Foundation, 59 Temple Place, Suite 330, Boston, MA 02111 USA.
 */

#ifndef __NLINKFS_H__

	#define __NLINKFS_H__

	#define FUSE_USE_VERSION 26
	#include <fuse.h>
	#include <fuse_opt.h>

	#define NLINKFS_MAGIC      "NLINKFS"
	#define NLINKFS_MAGIC_SIZE 7

	#define nlk_getdata ((struct _nlinkfs_data *) fuse_get_context()->private_data)

#ifdef DEBUG
	/* Write debug messages to file  */
	#define debugf(fmt, ...) { \
			FILE *fp; \
			fp=fopen("/tmp/nlinkfs", "a+"); \
			fprintf(fp, fmt, ##__VA_ARGS__); \
			fclose(fp); \
		}
#else
	/* Debug messages disabled */
	#define debugf(fmt, ...)
#endif

	/* nlinkfs data passed through fuse */
	struct _nlinkfs_data {
		char *srcdir;
	};

	typedef struct _nlinkfs_slink nlk_slink;
	typedef struct _nlinkfs_data nlk_data;

#endif

