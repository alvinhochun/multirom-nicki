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
 *
 * Copyright (c) 2013-2014 alvinhochun-at-gmail-com
 * Copyright (c) 2011-2013 vbocek-at-gmail-com
 *
 */

#include <stdio.h>
#include <stdlib.h>

#include "fstab.h"
#include "multirom_main.h"
#include "multirom_misc.h"
#include "multirom_partitions.h"
#include "multirom_rom.h"
#include "multirom_status.h"
#include "multirom_ui.h"
#include "util.h"
#include "log.h"

enum exit_status multirom(void)
{
    multirom_status.fstab = fstab_auto_load();
    if(multirom_status.fstab == NULL)
    {
        multirom_emergency_reboot();
        while(1);
    }

    multirom_mount_internal_storage();

    // TODO: load and save preferences
    //multirom_load_default_pref();
    multirom_status.pref.brightness = 40;

    multirom_scan_partitions();
    multirom_scan_all_roms();

    struct multirom_rom *to_boot;
    struct multirom_romdata *boot_profile;
    int ui_exit = multirom_ui(&to_boot, &boot_profile);

    enum exit_status exit = EXIT_REBOOT_RECOVERY;

    switch(ui_exit)
    {
        case UI_EXIT_BOOT_ROM:
            exit = multirom_prepare_boot(to_boot, boot_profile);
            break;
        case UI_EXIT_REBOOT:
            exit = EXIT_REBOOT;
            break;
        case UI_EXIT_REBOOT_RECOVERY:
//#if MR_COMBINEDROOT
#if 1
            /*
             * If the user selects "Recovery" in the misc menu, simply replace
             * boot.cpio with recovery.cpio to save some time.
             * However we want clean reboot for any other cases, so no need to
             * copy this code all over the place.
             */
            ERROR("Entering recovery, replacing boot.cpio with recovery.cpio...");
            remove("/multirom/boot.cpio");
            rename("/multirom/recovery.cpio", "/multirom/boot.cpio");
            exit = EXIT_NORMALBOOT;
#else
            exit = EXIT_REBOOT_RECOVERY;
#endif
            break;
        case UI_EXIT_REBOOT_BOOTLOADER:
            exit = EXIT_REBOOT_BOOTLOADER;
            break;
        case UI_EXIT_POWEROFF:
            exit = EXIT_POWEROFF;
            break;
    }

    return exit;
}

enum exit_status multirom_prepare_boot(struct multirom_rom *to_boot, struct multirom_romdata *boot_profile)
{
    switch(to_boot->type)
    {
    case ROM_TYPE_ANDROID_INT:
        switch(boot_profile->type)
        {
        case ROMDATA_TYPE_ANDROID_INT:
            return EXIT_NORMALBOOT;
        case ROMDATA_TYPE_ANDROID_IMG:
            return multirom_prepare_android_img(to_boot->partition, NULL, boot_profile->android_img);
        default:
            break;
        }
        break;
    case ROM_TYPE_ANDROID_IMG:
        switch(boot_profile->type)
        {
        case ROMDATA_TYPE_ANDROID_IMG:
            return multirom_prepare_android_img(to_boot->partition, to_boot->android_img, boot_profile->android_img);
        default:
            break;
        }
        break;
    default:
        break;
    }
    ERROR("Function not implemented!");
    multirom_emergency_reboot();
}

static char *replace_mountpath(const char *target, const char *old, const char *new)
{
    size_t oldlen = strlen(old);
    if(strncmp(target, old, oldlen) == 0)
    {
        size_t newlen = strlen(new);
        char *res = malloc(newlen + strlen(target + oldlen) + 1);
        strncpy(res, new, newlen);
        strcpy(res + newlen, target + oldlen);
        return res;
    }
    else
    {
        return NULL;
    }
}

enum exit_status multirom_prepare_android_img(struct multirom_partition *part, struct multirom_rom_android_img *sys, struct multirom_romdata_android_img *data)
{
    FILE *f = fopen("/multirom/setup-gen.sh", "w");
    if(f == NULL)
    {
        ERROR("Cannot open /multirom/setup-gen.sh");
        goto fail;
    }
    if(fputs("bb mkdir /multirom/mnt\n", f) < 0)
        goto fail;
    if(fprintf(f, "waitfor \"%s\"\n", part->block_dev) < 0)
        goto fail;
    if(fprintf(f, "bb mount -t %s \"%s\" /multirom/mnt\n", part->fstype, part->block_dev) < 0)
        goto fail;
    char *path;
    if(sys != NULL)
    {
        char *cmd;
        remove("/multirom/boot.cpio");
        asprintf(&cmd, "/multirom/busybox zcat \"%s\" > /multirom/boot.cpio", sys->ramdisk_path);
        int res = shell_cmd(cmd);
        free(cmd);
        if(res != 0)
        {
            ERROR("Extracting ramdisk.gz failed");
            goto fail;
        }

        path = replace_mountpath(sys->system_path, part->mount_path, "/multirom/mnt");
        if(part == NULL)
            goto fail;
        if(fprintf(f, "replaceblk \"/dev/block/platform/msm_sdcc.1/by-name/system\" \"%s\"\n", path) < 0)
            goto fail;
        free(path);

        if(sys->firmware_path != NULL)
        {
            path = replace_mountpath(sys->firmware_path, part->mount_path, "/multirom/mnt");
            if(part == NULL)
                goto fail;
            if(fprintf(f, "replaceblk \"/dev/block/platform/msm_sdcc.1/by-name/modem\" \"%s\"\n", path) < 0)
                goto fail;
            free(path);
        }
    }

    if(data == NULL)
        goto fail;

    path = replace_mountpath(data->data_path, part->mount_path, "/multirom/mnt");
    if(part == NULL)
        goto fail;
    if(fprintf(f, "replaceblk \"/dev/block/platform/msm_sdcc.1/by-name/userdata\" \"%s\"\n", path) < 0)
        goto fail;
    free(path);

    path = replace_mountpath(data->cache_path, part->mount_path, "/multirom/mnt");
    if(part == NULL)
        goto fail;
    if(fprintf(f, "replaceblk \"/dev/block/platform/msm_sdcc.1/by-name/cache\" \"%s\"\n", path) < 0)
        goto fail;
    free(path);

    path = replace_mountpath(data->persist_path, part->mount_path, "/multirom/mnt");
    if(part == NULL)
        goto fail;
    if(fprintf(f, "replaceblk \"/dev/block/platform/msm_sdcc.1/by-name/persist\" \"%s\"\n", path) < 0)
        goto fail;
    free(path);

    fclose(f);

    mkdir("/rd_tmp", 0755);
    if(shell_cmd("cd /rd_tmp; /multirom/busybox cpio -i < \"/multirom/boot.cpio\"") != 0)
    {
        ERROR("Cannot extract cpio ramdisk!");
        goto fail;
    }
    rename("/rd_tmp/init.rc", "/multirom/init.rc.orig");
    rename("/multirom/prepend-init.rc", "/rd_tmp/init.rc");

    FILE *app = fopen("/rd_tmp/init.rc", "a");
    if(app == NULL)
    {
        ERROR("Cannot open init.rc");
        goto fail;
    }

    FILE *in = fopen("/multirom/init.rc.orig", "r");
    if(in == NULL)
    {
        ERROR("Cannot open init.rc.orig");
        goto fail;
    }

    fseek(in, 0, SEEK_END);
    size_t size = ftell(in);
    rewind(in);

    char *buff = malloc(size);
    fread(buff, 1, size, in);
    fwrite(buff, 1, size, app);

    fclose(in);
    fclose(app);
    free(buff);

    remove("/multirom/init.rc.orig");

    if(sys != NULL && sys->kernel_path != NULL)
    {
        // kexec, copy busybox, etc. to new ramdisk
        mkdir("/rd_tmp/multirom", 0755);
        copy_file("/multirom/busybox", "/rd_tmp/multirom/busybox");
        chmod("/rd_tmp/multirom/busybox", 0755);
        copy_file("/multirom/reboot", "/rd_tmp/multirom/reboot");
        chmod("/rd_tmp/multirom/reboot", 0755);
        copy_file("/multirom/setup.sh", "/rd_tmp/multirom/setup.sh");
        chmod("/rd_tmp/multirom/setup.sh", 0755);
        copy_file("/multirom/setup-gen.sh", "/rd_tmp/multirom/setup-gen.sh");
    }

    remove("/multirom/boot.cpio");

    if(shell_cmd("cd /rd_tmp; /multirom/busybox find . | /multirom/busybox cpio -H newc -o > /multirom/boot.cpio") != 0)
    {
        ERROR("Cannot repack ramdisk!");
        goto fail;
    }

    remove_dir("/rd_tmp");

    if(sys != NULL && sys->kernel_path != NULL)
    {
        char *bl_cmdline = multirom_get_bootloader_cmdline();
        if(bl_cmdline == NULL)
        {
            ERROR("Cannot get bootloader cmdline");
            goto fail;
        }
        size_t l1 = strlen(sys->cmdline);
        size_t l2 = strlen(bl_cmdline);
        char *new_cmdline = malloc(l1 + l2 + 1);
        strncpy(new_cmdline, sys->cmdline, l1);
        strcpy(new_cmdline + l1, bl_cmdline);
        enum exit_status res = multirom_prepare_kexec(sys->kernel_path, "/multirom/boot.cpio", new_cmdline);
        free(new_cmdline);
        return res;
    }
    else
        return EXIT_NORMALBOOT;
fail:
    multirom_emergency_reboot();
}

enum exit_status multirom_prepare_kexec(const char *kernel_path, const char *ramdisk, const char *cmdline)
{
    char *arg_ramdisk = malloc(strlen("--initrd=") + strlen(ramdisk) + 1);
    char *arg_cmdline = malloc(strlen("--command-line=") + strlen(cmdline) + 1);
    const char *cmd[] = {
        "/multirom/kexec",               // 0
        "--load-hardboot",               // 1
        kernel_path,                     // 2 - path to zImage
        "--mem-min="MR_KEXEC_MEM_MIN,    // 3
        arg_ramdisk,                     // 4 - --initrd=<path to initrd>
        arg_cmdline,                     // 5 - --command-line=<cmdline>
#ifdef MR_KEXEC_DTB
        "--dtb",                         // 6
#endif
        NULL
    };
    strcpy(arg_ramdisk, "--initrd=");
    strcat(arg_ramdisk, ramdisk);
    strcpy(arg_cmdline, "--command-line=");
    strcat(arg_cmdline, cmdline);

    ERROR("Loading kexec with args:");
    const char **ptr;
    for(ptr = cmd; *ptr != NULL; ptr++)
        ERROR("%s", *ptr);

    if(run_cmd((char *const *)cmd) == 0)
    {
        return EXIT_KEXEC;
    }
    else
    {
        ERROR("Loading kexec failed");
        multirom_emergency_reboot();
    }
}
