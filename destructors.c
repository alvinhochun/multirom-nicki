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
 *
 */

#include <stdlib.h>
#include <sys/mount.h>

#include "multirom_partitions.h"
#include "multirom_rom.h"
#include "util.h"

void free_multirom_rom_android_img(struct multirom_rom_android_img *p)
{
    if(p == NULL)
        return;
    free(p->kernel_path);
    free(p->ramdisk_path);
    free(p->system_path);
    free(p->firmware_path);
    free(p);
}

void free_multirom_romdata_android_img(struct multirom_romdata_android_img *p)
{
    if(p == NULL)
        return;
    free(p->data_path);
    free(p->cache_path);
    free(p->persist_path);
    free(p);
}

void free_multirom_romdata(struct multirom_romdata *p)
{
    if(p == NULL)
        return;
    free(p->name);
    switch(p->type)
    {
    case ROMDATA_TYPE_ANDROID_IMG:
        free_multirom_romdata_android_img(p->android_img);
        break;
    default:
        break;
    }
    free(p);
}

void free_multirom_rom(struct multirom_rom *p)
{
    if(p == NULL)
        return;
    free(p->name);
    if(p->romdata_list != NULL)
    {
        list_clear(&p->romdata_list, free_multirom_romdata);
    }
    switch(p->type)
    {
    case ROM_TYPE_ANDROID_IMG:
        free_multirom_rom_android_img(p->android_img);
        break;
    default:
        break;
    }
    free(p);
}

void free_multirom_partition(struct multirom_partition *p)
{
    if(p == NULL)
        return;
    free(p->name);
    free(p->block_dev);
    if(p->mount_path != NULL)
    {
        umount(p->mount_path);
        free(p->mount_path);
    }
    free(p->uuid);
    free(p->fstype);
    free(p);
}
