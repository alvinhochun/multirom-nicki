/*
 * This file is part of MultiROM.
 *
 * MultiROM is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MultiROM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MultiROM.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sys/stat.h>
#include <sys/mount.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <cutils/android_reboot.h>

#include "devices.h"
#include "log.h"
#include "../util.h"
#include "../version.h"
#include "adb.h"
#include "../fstab.h"
#include "../hooks.h"

#define EXEC_MASK (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
#define REALDATA "/realdata"
#define MULTIROM_BIN "multirom"
#define BUSYBOX_BIN "busybox"

// Not defined in android includes?
#define MS_RELATIME (1<<21)

static char path_multirom[] = "/multirom";

static void clean_mnt_mounts(void)
{
    DIR *d = opendir("/mnt");
    if(d != NULL)
    {
        struct dirent *dr;
        while((dr = readdir(d)) != NULL)
        {
            if(dr->d_name[0] == '.')
                continue;

            if(dr->d_type != DT_DIR)
                continue;

            char buf[256];
            strncpy(buf, "/mnt/", strlen("/mnt/"));
            strncpy(buf + strlen("/mnt"), dr->d_name, sizeof(buf) - strlen("/mnt"));
            // FIXME: handle properly instead of truncating string
            buf[sizeof(buf) - 1] = '\0';
            umount(buf);
        }
        closedir(d);
    }
    umount("/mnt");
}

static int run_multirom_bin(char *path)
{
    ERROR("Running multirom");
    int status = run_cmd((char *const[]){ path, NULL });
    ERROR("MultiROM exited with status %d", status);
    return status;
}

static void run_multirom(void)
{
    char path[256];
    struct stat info;

    // busybox
    sprintf(path, "%s/%s", path_multirom, BUSYBOX_BIN);
    if (stat(path, &info) < 0)
    {
        ERROR("Could not find busybox: %s", path);
        return;
    }
    chmod(path, EXEC_MASK);

    // multirom
    sprintf(path, "%s/%s", path_multirom, MULTIROM_BIN);
    if (stat(path, &info) < 0)
    {
        ERROR("Could not find multirom: %s", path);
        return;
    }
    chmod(path, EXEC_MASK);

    int i;
    for(i = 0; i < 3; ++i)
    {
        if(run_multirom_bin(path) == 0)
            break;
        ERROR("MultiROM probably crashed!");
        clean_mnt_mounts(); // MultiROM may have mounted partitions, so umount them
        mount("tmpfs", "/mnt", "tmpfs", 0, ""); // mount a clean tmpfs
    }
    if(i == 3)
    {
        ERROR("MultiROM crashed 3 times! Rebooting into recovery");
        android_reboot(ANDROID_RB_RESTART2, 0, "recovery");
        while(1);
    }
}

static int is_charger_mode()
{
    char buff[2048] = { 0 };

    FILE *f = fopen("/proc/cmdline", "r");
    if(!f)
        return 0;

    fgets(buff, sizeof(buff), f);
    fclose(f);

    return (strstr(buff, "androidboot.mode=charger") != NULL);
}

static int is_reboot_recovery()
{
    char buff[2048] = { 0 };

    FILE *f = fopen("/proc/cmdline", "r");
    if(!f)
        return 0;

    fgets(buff, sizeof(buff), f);
    fclose(f);

    // See kernel source arch/arm/mach-msm/restart.c at msm_reboot_call
    return (strstr(buff, "warmboot=0x77665502") != NULL);
}

static int should_enter_recovery(struct fstab *fstab)
{
    if(is_reboot_recovery())
    {
        ERROR("Reboot recovery detected");
        return 1;
    }

    if(!fstab)
        return 0;

    struct fstab_part *p = fstab_find_by_path(fstab, "/cache");
    if(!p)
    {
        ERROR("Failed to find /cache partition in fstab");
        return 0;
    }

    if(wait_for_file(p->device, 5) < 0)
    {
        ERROR("Waiting too long for dev %s", p->device);
        return 0;
    }

    mkdir("/cache", 0755);

    if (mount(p->device, "/cache", p->type, p->mountflags, p->options) < 0)
    {
        ERROR("Failed to mount /cache %d\n", errno);
        rmdir("/cache");
        return 0;
    }

    int ret = access("/cache/recovery/boot", F_OK) == 0;
    if(ret)
        remove("/cache/recovery/boot");

    umount("/cache");
    rmdir("/cache");
    return ret;
}

int main(int argc, char *argv[])
{
    int i, res;
    struct fstab *fstab = NULL;

    for(i = 1; i < argc; ++i)
    {
        if(strcmp(argv[i], "-v") == 0)
        {
            printf("%d\n", VERSION_TRAMPOLINE);
            fflush(stdout);
            return 0;
        }
    }

    umask(000);

    // Init only the little we need, leave the rest for real init
    mkdir("/dev", 0755);
    mount("tmpfs", "/dev", "tmpfs", MS_NOSUID, "mode=0755");
    mkdir("/dev/pts", 0755);
    mount("devpts", "/dev/pts", "devpts", 0, "");
    mkdir("/dev/socket", 0755);
    mkdir("/proc", 0755);
    mount("proc", "/proc", "proc", 0, "");
    mkdir("/sys", 0755);
    mount("sysfs", "/sys", "sysfs", 0, "");
    mkdir("/mnt", 0755);
    mount("tmpfs", "/mnt", "tmpfs", 0, "");

    klog_init();
    ERROR("Running trampoline v%d\n", VERSION_TRAMPOLINE);

    if(is_charger_mode())
    {
        ERROR("Charger mode detected, skipping multirom\n");
    }
    else
    {
        ERROR("Initializing devices...");
        devices_init();
        ERROR("Done initializing");

        fstab = fstab_auto_load();
        if(should_enter_recovery(fstab))
        {
            ERROR("Entering recovery, replacing boot.cpio with recovery.cpio...");
            remove("/multirom/boot.cpio");
            rename("/multirom/recovery.cpio", "/multirom/boot.cpio");
        }
        else if(fstab)
        {
#if 0
            fstab_dump(fstab); //debug
#endif
            if(wait_for_file("/dev/graphics/fb0", 5) >= 0)
            {
                adb_init(path_multirom);
                run_multirom();
                adb_quit();
            }
            else
            {
                ERROR("Waiting too long for fb0");
            }
        }
        else
        {
            ERROR("Cannot load fstab, skipping MultiROM");
        }

        if(fstab)
            fstab_destroy(fstab);

        // close and destroy everything
        devices_close();
    }

    clean_mnt_mounts();
    remove("/mnt");

    umount("/dev/pts");
    rmdir("/dev/pts");
    rmdir("/dev/socket");
    umount("/dev");
    rmdir("/dev");
    umount("/proc");
    rmdir("/proc");
    umount("/sys");
    rmdir("/sys");

    remove("/init");
    remove("/multirom/fstab");
    remove("/multirom/multirom");
    remove("/multirom/adbd");
    remove("/multirom/kexec");

    ERROR("extracting boot.cpio...");
    int status = shell_cmd("cd /; /multirom/busybox cpio -i < /multirom/boot.cpio");
    if(status != 0)
    {
        ERROR("Cannot extract boot.cpio! Status %d", status);
    }

    remove("/multirom/boot.cpio");

    static char *const cmd[] = { "/init", NULL };
    chmod(cmd[0], EXEC_MASK);
    chdir("/");

    ERROR("Running main init\n");
    // run the main init
    res = execve(cmd[0], cmd, NULL);
    ERROR("execve returned %d %d %s\n", res, errno, strerror(errno));
    return 0;
}
