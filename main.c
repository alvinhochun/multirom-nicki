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

#include <stdlib.h>
#include <unistd.h>
#include <cutils/android_reboot.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mount.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include "multirom_main.h"
#include "multirom_misc.h"
#include "framebuffer.h"
#include "log.h"
#include "version.h"
#include "util.h"

#define EXEC_MASK (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)

static void __attribute__((noreturn)) do_reboot(int exit)
{
    sync();

    if(exit & EXIT_REBOOT_RECOVERY)         android_reboot(ANDROID_RB_RESTART2, 0, "recovery");
    else if(exit & EXIT_REBOOT_BOOTLOADER)  android_reboot(ANDROID_RB_RESTART2, 0, "bootloader");
    else if(exit & EXIT_POWEROFF)           android_reboot(ANDROID_RB_POWEROFF, 0, NULL);
    else                                    android_reboot(ANDROID_RB_RESTART, 0, NULL);

    while(1);
}

/* Check to see if /proc/mounts contains any writeable filesystems
 * backed by a block device.
 * Return true if none found, else return false.
 */
static int remount_ro_done(void)
{
    FILE *f;
    char mount_dev[256];
    char mount_dir[256];
    char mount_type[256];
    char mount_opts[256];
    int mount_freq;
    int mount_passno;
    int match;
    int found_rw_fs = 0;

    f = fopen("/proc/mounts", "r");
    if (! f) {
        /* If we can't read /proc/mounts, just give up */
        return 1;
    }

    do {
        match = fscanf(f, "%255s %255s %255s %255s %d %d\n",
                       mount_dev, mount_dir, mount_type,
                       mount_opts, &mount_freq, &mount_passno);
        mount_dev[255] = 0;
        mount_dir[255] = 0;
        mount_type[255] = 0;
        mount_opts[255] = 0;
        if ((match == 6) && !strncmp(mount_dev, "/dev/block", 10) && strstr(mount_opts, "rw")) {
            found_rw_fs = 1;
            break;
        }
    } while (match != EOF);

    fclose(f);

    return !found_rw_fs;
}

/* Remounting filesystems read-only is difficult when there are files
 * opened for writing or pending deletes on the filesystem.  There is
 * no way to force the remount with the mount(2) syscall.  The magic sysrq
 * 'u' command does an emergency remount read-only on all writable filesystems
 * that have a block device (i.e. not tmpfs filesystems) by calling
 * emergency_remount(), which knows how to force the remount to read-only.
 * Unfortunately, that is asynchronous, and just schedules the work and
 * returns.  The best way to determine if it is done is to read /proc/mounts
 * repeatedly until there are no more writable filesystems mounted on
 * block devices.
 */
static void remount_ro(void)
{
    int fd, cnt = 0;

    /* Trigger the remount of the filesystems as read-only,
     * which also marks them clean.
     */
    fd = open("/proc/sysrq-trigger", O_WRONLY);
    if (fd < 0) {
        return;
    }
    write(fd, "u", 1);
    close(fd);


    /* Now poll /proc/mounts till it's done */
    while (!remount_ro_done() && (cnt < 50)) {
        usleep(100000);
        cnt++;
    }

    return;
}

static __attribute__((noreturn)) void do_kexec(void)
{
    sync();

    // Force remount ro by triggering a sysrq
    // This is necessary to prevent data corruption because kexec does not do this
    remount_ro();

    execl("/multirom/kexec", "/multirom/kexec", "-e", NULL);

    ERROR("kexec -e failed! (%d: %s)", errno, strerror(errno));
    multirom_emergency_reboot();
    while(1);
}

int main(int argc, char *argv[])
{
    int i;
    for(i = 1; i < argc; ++i)
    {
        if(strcmp(argv[i], "-v") == 0)
        {
            printf("%d%s\n", VERSION_MULTIROM, VERSION_DEV_FIX);
            fflush(stdout);
            return 0;
        }
    }

    srand(time(0));
    klog_init();

    // output all messages to dmesg,
    // but it is possible to filter out INFO messages
    klog_set_level(6);

    ERROR("Running MultiROM v%d%s\n", VERSION_MULTIROM, VERSION_DEV_FIX);

    int exit = multirom();

    if(exit >= 0)
    {
        if(exit & EXIT_REBOOT_MASK)
        {
            do_reboot(exit);
        }

        if(exit & EXIT_KEXEC)
        {
            do_kexec();
        }
    }

    vt_set_mode(0);

    return 0;
}
