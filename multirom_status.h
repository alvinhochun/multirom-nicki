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

#ifndef MULTIROM_STATUS_H_
#define MULTIROM_STATUS_H_

#include "multirom_partitions.h"
#include "multirom_pref.h"
#include "multirom_rom.h"
#include "fstab.h"

struct multirom_status
{
    struct fstab *fstab;
    struct multirom_partition **partitions; // A list of partitions
    struct multirom_rom **roms; // A list of ROMs
    char *external_sd; // Path to external sd mount point or NULL
    struct multirom_pref pref;
};

extern struct multirom_status multirom_status;

#endif /* MULTIROM_STATUS_H_ */
