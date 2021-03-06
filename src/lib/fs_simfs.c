/*
 *  Copyright (C) 2000-2009, Parallels, Inc. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdlib.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <limits.h>

#include "types.h"
#include "fs.h"
#include "logger.h"
#include "vzerror.h"

/* Check if CT private area is mounted
 * Returns:
 *   1: mounted
 *   0: not mounted
 *  -1: error getting status
 */
int vps_is_mounted(const fs_param *fs)
{
	struct stat st1, st2;
	char parent[PATH_MAX];
	const char *root = fs->root;
	const char *private = fs->private;

	if (!root || !private)
		return -1;
#if TREAT_SEMI_MOUNTED_PLOOP_AS_MOUNTED
	/* There can be a weird case -- CT is on ploop, ploop itself
	 * is mounted but the filesystem is not -- some "semi-mounted"
	 * state.  We consider this state as not mounted, because
	 * there's no data in VE_PRIVATE.
	 * Because of this consideration, below code is disabled.
	 * If you'll ever want to enable it, please make sure to
	 * #include "image.h"
	 */
	if (fs->layout == VE_LAYOUT_PLOOP && !is_ploop_supported())
		return VZ_PLOOP_UNSUP;
#ifdef HAVE_PLOOP
	return is_image_mounted(private);
#endif

#endif

	if (stat(root, &st1)) {
		logger(-1, errno, "stat(%s)", root);
		return -1;
	}

	snprintf(parent, sizeof(parent), "%s/..", root);
	if (stat(parent, &st2)) {
		logger(-1, errno, "stat(%s)", parent);
		return -1;
	}

	/* Check for real mount (simfs or ploop) */
	if (st1.st_dev != st2.st_dev)
		return 1;

	/* Check for bind mount (upstream case) */
	if (stat(private, &st2)) {
		/* return false when private area does not exist */
		if (errno == ENOENT)
			return 0;
		logger(-1, errno, "stat(%s)", private);
		return -1;
	}

	return (st1.st_dev == st2.st_dev) && (st1.st_ino == st2.st_ino);
}

static char *fs_name = "simfs";
const char *vz_fs_get_name()
{
	return fs_name;
}

int vz_mount(fs_param *fs, int flags)
{
	int mntopt = fs->flags | flags;
	const int remount = flags & MS_REMOUNT;

	logger(2, 0,  "Mounting root: %s %s", fs->root, fs->private);
	if (mount(fs->private, fs->root, "simfs", mntopt,
			remount ? "" : fs->private) < 0)
	{
		logger(-1, errno, "Can't mount: %s %s", fs->root, fs->private);
		if (errno == ENODEV)
			logger(-1, 0, "Kernel lacks simfs support. Please "
				"compile it in, or load simfs module.");
		return VZ_FS_CANTMOUNT;
	}
	return 0;
}
