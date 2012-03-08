/*
 *  Copyright (C) 2000-2011, Parallels, Inc. All rights reserved.
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

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <mntent.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/param.h>

#include "fs.h"
#include "util.h"
#include "logger.h"
#include "vzerror.h"
#include "script.h"
#include "quota.h"
#include "image.h"

int vps_is_run(vps_handler *h, envid_t veid);

int vps_is_mounted(const char *root)
{
	return vz_fs_is_mounted(root);
}

int fsmount(envid_t veid, fs_param *fs, dq_param *dq)
{
	int ret;

	/* Create VE_ROOT mount point if not exist */
	if (make_dir(fs->root, 1)) {
		logger(-1, 0, "Can't create mount point %s", fs->root);
		return VZ_FS_MPOINTCREATE;
	}
	if (ve_private_is_ploop(fs->private)) {
		struct vzctl_mount_param param = {};

		param.target = fs->root;
		param.quota = is_2nd_level_quota_on(dq);

		ret = vzctl_mount_image(fs->private, &param);
	}
	else {
		if ((ret = vps_quotaon(veid, fs->private, dq)))
			return ret;
		if ((ret = vz_mount(fs, 0)))
			vps_quotaoff(veid, dq);
	}
	return ret;
}

int fsumount(envid_t veid, const fs_param *fs)
{
	if (umount(fs->root) != 0) {
		logger(-1, errno, "Can't umount %s", fs->root);
		return VZ_FS_CANTUMOUNT;
	}

	if (!quota_ctl(veid, QUOTA_STAT))
		return quota_off(veid, 0);

	return 0;
}

int vps_mount(vps_handler *h, envid_t veid, fs_param *fs, dq_param *dq,
	skipFlags skip)
{
	char buf[PATH_LEN];
	int ret, i;

	if (check_var(fs->root, "VE_ROOT is not set"))
		return VZ_VE_ROOT_NOTSET;
	if (check_var(fs->private, "VE_PRIVATE is not set"))
		return VZ_VE_PRIVATE_NOTSET;
	if (!stat_file(fs->private)) {
		logger(-1, 0, "Container private area %s does not exist",
				fs->private);
		return VZ_FS_NOPRVT;
	}
	if (vps_is_mounted(fs->root)) {
		logger(-1, 0, "Container is already mounted");
		return 0;
	}
	/* Execute pre mount scripts */
	if (!(skip & SKIP_ACTION_SCRIPT)) {
		snprintf(buf, sizeof(buf), "%svps.%s", VPS_CONF_DIR,
			PRE_MOUNT_PREFIX);
		for (i = 0; i < 2; i++) {
			if (run_pre_script(veid, buf)) {
				logger(-1, 0, "Error executing mount script %s",
					buf);
				fsumount(veid, fs);
				return VZ_ACTIONSCRIPT_ERROR;
			}
			snprintf(buf, sizeof(buf), "%s%d.%s", VPS_CONF_DIR,
				veid, PRE_MOUNT_PREFIX);
		}
	}
	if ((ret = fsmount(veid, fs, dq)))
		return ret;
	/* Execute per-CT & global mount scripts */
	if (!(skip & SKIP_ACTION_SCRIPT)) {
		snprintf(buf, sizeof(buf), "%svps.%s", VPS_CONF_DIR,
			MOUNT_PREFIX);
		for (i = 0; i < 2; i++) {
			if (run_pre_script(veid, buf)) {
				logger(-1, 0, "Error executing mount script %s",
					buf);
				fsumount(veid, fs);
				return VZ_ACTIONSCRIPT_ERROR;
			}
			snprintf(buf, sizeof(buf), "%s%d.%s", VPS_CONF_DIR,
				veid, MOUNT_PREFIX);
		}
	}
	logger(0, 0, "Container is mounted");

	return 0;
}

static int umount_submounts(const char *root)
{
	FILE *fp;
	struct mntent *mnt;
	int len;
	char path[MAXPATHLEN + 1];

	if (realpath(root, path) == NULL) {
		logger(-1, errno, "realpath(%s) failed", root);
		return -1;
	}
	if ((fp = setmntent("/proc/mounts", "r")) == NULL) {
		logger(-1, errno, "Unable to open /proc/mounts");
		return -1;
	}
	strcat(path, "/"); /* skip base mountpoint */
	len = strlen(path);
	while ((mnt = getmntent(fp)) != NULL) {
		if (strncmp(path, mnt->mnt_dir, len) == 0) {
			if (umount(mnt->mnt_dir))
				logger(-1, errno, "Cannot umount %s",
						mnt->mnt_dir);
		}
	}
	endmntent(fp);

	return 0;
}

int vps_umount(vps_handler *h, envid_t veid, const fs_param *fs,
		skipFlags skip)
{
	char buf[PATH_LEN];
	int ret, i;

	if (!vps_is_mounted(fs->root)) {
		logger(-1, 0, "CT is not mounted");
		return VZ_FS_NOT_MOUNTED;
	}
	if (vps_is_run(h, veid)) {
		logger(-1, 0, "Container is running -- stop it first");
		return 0;
	}
	if (!(skip & SKIP_ACTION_SCRIPT)) {
		snprintf(buf, sizeof(buf), "%s%d.%s", VPS_CONF_DIR,
			veid, UMOUNT_PREFIX);
		for (i = 0; i < 2; i++) {
			if (run_pre_script(veid, buf)) {
				logger(-1, 0, "Error executing umount script %s",
					buf);
				return VZ_ACTIONSCRIPT_ERROR;
			}
			snprintf(buf, sizeof(buf), "%svps.%s", VPS_CONF_DIR,
				UMOUNT_PREFIX);
		}
	}
	umount_submounts(fs->root);
	if (!(ret = fsumount(veid, fs)))
		logger(0, 0, "Container is unmounted");
	if (!(skip & SKIP_ACTION_SCRIPT)) {
		snprintf(buf, sizeof(buf), "%s%d.%s", VPS_CONF_DIR,
			veid, POST_UMOUNT_PREFIX);
		for (i = 0; i < 2; i++) {
			if (run_pre_script(veid, buf)) {
				logger(-1, 0, "Error executing umount script %s",
					buf);
				return VZ_ACTIONSCRIPT_ERROR;
			}
			snprintf(buf, sizeof(buf), "%svps.%s", VPS_CONF_DIR,
				POST_UMOUNT_PREFIX);
		}
	}

	return ret;
}

int vps_set_fs(fs_param *g_fs, fs_param *fs)
{
	if (fs->noatime != YES)
		return 0;
	if (check_var(g_fs->root, "VE_ROOT is not set"))
		return VZ_VE_ROOT_NOTSET;
	if (check_var(g_fs->private, "VE_PRIVATE is not set"))
		return VZ_VE_PRIVATE_NOTSET;
	if (!vps_is_mounted(g_fs->root)) {
		logger(-1, 0, "Container is not mounted");
		return VZ_FS_NOT_MOUNTED;
	}
	g_fs->noatime = fs->noatime;
	return vz_mount(g_fs, 1);
}
