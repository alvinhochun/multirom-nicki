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

#include <stdlib.h>
#include <sys/mount.h>
#include <errno.h>

#include "fstab.h"
#include "multirom_partitions.h"
#include "multirom_status.h"
#include "util.h"
#include "log.h"

#define BLOCKDEV_PREFIX "/dev/block/"

void multirom_scan_partitions(void)
{
    if(multirom_status.partitions_external != NULL)
        return;

    INFO("Scanning for partitions...");

    char *const cmd[] = { "/multirom/busybox", "blkid", NULL };
    char *res = run_get_stdout(cmd);
    if(res == NULL)
    {
        ERROR("Scanning for external partitions failed!");
        return;
    }

    char *line = strtok(res, "\n");
    if(line == NULL)
    {
        ERROR("Scanning for external partitions failed!");
        free(res);
        return;
    }

    do
    {
        INFO("Current entry: %s", line);

        if(strncmp(line, BLOCKDEV_PREFIX, strlen(BLOCKDEV_PREFIX)) != 0)
            continue;
        char *curtok = line + strlen(BLOCKDEV_PREFIX);
        char *curtok_end = strstr(curtok, ": ");
        if(curtok_end == NULL)
            continue;
        curtok_end[0] = '\0';

        if(strchr(curtok, '/') != NULL)
            continue;

        struct multirom_partition *part;
        if(strncmp(curtok, "mmcblk", strlen("mmcblk")) == 0)
        {
            // FIXME: This always assumes mmcblk0 is internal emmc
            if(strncmp(curtok + strlen("mmcblk"), "0p", strlen("0p")) == 0)
            {
                INFO("Skipping internal emmc %s", curtok);
                continue; // ignore internal emmc
            }

            part = mzalloc(sizeof(struct multirom_partition));
            part->type = PART_EXTERNAL_SD;
        }
        else
        {
            part = mzalloc(sizeof(struct multirom_partition));
            part->type = PART_EXTERNAL_USBDISK;
        }

        part->block_dev = strdup(line);
        part->name = strdup(curtok);
        part->mount_path = malloc(strlen("/mnt/") + strlen(curtok) + 1);
        strncpy(part->mount_path, "/mnt/", strlen("/mnt/"));
        strcpy(part->mount_path + strlen("/mnt/"), curtok);

        // TOFO: Handle the case where volume label contains ' UUID="'???
        curtok = strstr(curtok_end + 1, " UUID=\"");
        if(curtok == NULL)
        {
            ERROR("Partition %s does not have an UUID!", part->name);
            free_multirom_partition(part);
            continue;
        }

        curtok += strlen(" UUID=\"");
        curtok_end = strchr(curtok, '"');
        if(curtok_end == NULL)
        {
            free_multirom_partition(part);
            continue;
        }

        part->uuid = strndup(curtok, curtok_end - curtok);

        // HACK: Is it OK to assume that type follows uuid?
        //       I see busybox does that (at least for the one on cm-10.2).
        curtok = strstr(curtok_end + 1, " TYPE=\"");
        if(curtok == NULL)
        {
            ERROR("Partition %s does not have a type!", part->name);
            free_multirom_partition(part);
            continue;
        }

        curtok += strlen(" TYPE=\"");
        curtok_end = strchr(curtok, '"');
        if(curtok_end == NULL)
        {
            free_multirom_partition(part);
            continue;
        }

        part->fstype = strndup(curtok, curtok_end - curtok);

        if(multirom_mount_partition(part))
        {
            ERROR("Found and mounted partition %s, type=%s, UUID=%s", part->name, part->fstype, part->uuid);
            list_add(part, &multirom_status.partitions_external);
            if((strcmp(part->name, "mmcblk1") == 0 || strcmp(part->name, "mmcblk1p1") == 0)
                && multirom_status.external_sd == NULL)
                multirom_status.external_sd = strdup(part->mount_path);
        }
        else
        {
            ERROR("Failed to mounted partition %s, type=%s, UUID=%s", part->name, part->fstype, part->uuid);
            free_multirom_partition(part);
        }
    } while((line = strtok(NULL, "\n")) != NULL);
}

void multirom_clear_partitions(void)
{
    // *** DO NOT UMOUNT INTERNAL STORAGE HERE ***
    INFO("Unmounting and removing partitions...");
    struct multirom_partition **part_ptr;
    for(part_ptr = multirom_status.partitions_external; part_ptr != NULL && *part_ptr != NULL; part_ptr++)
    {
        umount((*part_ptr)->mount_path);
    }
    list_clear(&multirom_status.partitions_external, free_multirom_partition);

    if(multirom_status.external_sd != NULL)
    {
        free(multirom_status.external_sd);
        multirom_status.external_sd = NULL;
    }
}

void multirom_mount_internal_storage(void)
{
    // TODO: Support non-datamedia internal emmc
    struct fstab_part *fstab_part = fstab_find_by_path(multirom_status.fstab, "/data");
    if(fstab_part == NULL)
    {
        ERROR("Cannot get internal data partition.");
        multirom_status.partition_internal = multirom_mount_fake_internal_storage();
        return;
    }
    wait_for_file(fstab_part->device, 5);
    mkdir("/mnt/data", 0755);
    if(mount(fstab_part->device, "/mnt/data", fstab_part->type, fstab_part->mountflags, fstab_part->options) != 0)
    {
        ERROR("Cannot mount internal data partition.");
        multirom_status.partition_internal = multirom_mount_fake_internal_storage();
        return;
    }
    struct multirom_partition *part = mzalloc(sizeof(struct multirom_partition));
    part->name = strdup("internal");
    part->mount_path = strdup("/mnt/internal");
    part->uuid = strdup("INTERNAL");
    part->fstype = strdup(fstab_part->type);
    part->type = PART_INTERNAL;
    mkdir("/mnt/internal", 0755);
    if(mount("/mnt/data/media/0", "/mnt/internal", "", MS_BIND, "") == 0)
    {
        ERROR("/data/media/0 is mounted as internal storage.");
        multirom_status.partition_internal = part;
    }
    else if(mount("/mnt/data/media", "/mnt/internal", "", MS_BIND, "") == 0)
    {
        ERROR("/data/media is mounted as internal storage.");
        multirom_status.partition_internal = part;
    }
    else
    {
        free_multirom_partition(part);
        multirom_status.partition_internal = multirom_mount_fake_internal_storage();
    }
}

struct multirom_partition *multirom_mount_fake_internal_storage(void)
{
    ERROR("Cannot mount internal storage, creating a fake one instead");
    // Doesn't really matter, just can't use pref and no internal storage
    mkdir("/mnt/internal", 0755);
    if(mount("tmpfs", "/mnt/internal", "tmpfs", 0, "") != 0)
    {
        ERROR("Cannot even mount a tmpfs. What a Terrible Failure.");
        ERROR("Just cross my fingers hoping that this thing won't crash badly...");
    }
    struct multirom_partition *part = mzalloc(sizeof(struct multirom_partition));
    part->name = strdup("internal");
    part->mount_path = strdup("/mnt/internal");
    part->uuid = strdup("FAKESTOR");
    part->fstype = strdup("tmpfs");
    part->type = PART_INTERNAL;
    return part;
}

int multirom_mount_partition(struct multirom_partition *part)
{
    mkdir(part->mount_path, 0755);
    if(mount(part->block_dev, part->mount_path, part->fstype, MS_NOATIME, "") == 0)
    {
        return 1;
    }
    else
    {
        ERROR("Cannot mount. Error: %d %s", errno, strerror(errno));
        return 0;
    }
}
