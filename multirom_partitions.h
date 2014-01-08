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

#ifndef MULTIROM_PARTITIONS_H_
#define MULTIROM_PARTITIONS_H_

enum multirom_partition_type
{
    PART_UNKNOWN = 0,

    PART_INTERNAL,
    PART_EXTERNAL_SD,
    PART_EXTERNAL_USBDISK,
};

struct multirom_partition
{
    char *name;
    char *block_dev;
    char *mount_path;
    char *uuid;
    char *fstype;
    enum multirom_partition_type type;
};

void free_multirom_partition(struct multirom_partition *p);

void multirom_scan_partitions(void);
void multirom_clear_partitions(void);
void multirom_mount_internal_storage(void);
struct multirom_partition *multirom_mount_fake_internal_storage(void);
int multirom_mount_partition(struct multirom_partition *part);

#endif /* MULTIROM_PARTITIONS_H_ */
