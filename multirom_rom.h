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
 * Copyright (c) 2013 alvinhochun-at-gmail-com
 *
 */

#ifndef MULTIROM_ROM_H_
#define MULTIROM_ROM_H_

#include "multirom_partitions.h"

enum
{
    ROMTYPE_FMT_IMG = 1,    // Image format
    ROMTYPE_FMT_INT = 255,  // Internal

    ROMTYPE_OS_ANDROID = 1 << 8,   // Android
};

#define ROMTYPE_FMT(x) (x & 0xff)
#define ROMTYPE_OS(x) (x & (0xff << 8))

enum multirom_rom_type
{
    ROM_TYPE_UNKNOWN = 0,

    ROM_TYPE_ANDROID_IMG = ROMTYPE_FMT_IMG | ROMTYPE_OS_ANDROID, // External ROMs in disk image format
    ROM_TYPE_ANDROID_INT = ROMTYPE_FMT_INT | ROMTYPE_OS_ANDROID, // Internal ROM
};

struct multirom_rom_android_img
{
    char *kernel_path;      // can be NULL, not NULL == kexec
    char *cmdline;          // if kernel_path is not null, this must not be null
    char *ramdisk_path;
    char *system_path;
    char *firmware_path;    // can be NULL
};

void free_multirom_rom_android_img(struct multirom_rom_android_img *p);

enum multirom_romdata_type
{
    ROMDATA_TYPE_UNKNOWN = 0,

    ROMDATA_TYPE_ANDROID_IMG = ROMTYPE_FMT_IMG | ROMTYPE_OS_ANDROID, // disk image format
    ROMDATA_TYPE_ANDROID_INT = ROMTYPE_FMT_INT | ROMTYPE_OS_ANDROID, // internal data
};

struct multirom_romdata_android_img
{
    char *data_path;
    char *cache_path;
    char *persist_path;
};

void free_multirom_romdata_android_img(struct multirom_romdata_android_img *p);

/*
 * ROM userdata, data
 */
struct multirom_romdata
{
    char *name;
    enum multirom_romdata_type type;
    union
    {
        struct multirom_romdata_android_img *android_img;
    };
};

void free_multirom_romdata(struct multirom_romdata *p);

/*
 * ROM system information
 */
struct multirom_rom
{
    char *name;
    struct multirom_partition *partition;
    struct multirom_romdata **romdata_list; // array of pointer to a userdata
    enum multirom_rom_type type;
    union
    {
        struct multirom_rom_android_img *android_img;
    };
};

void free_multirom_rom(struct multirom_rom *p);

struct multirom_rom **multirom_scan_roms(struct multirom_partition *partition);
struct multirom_rom *multirom_parse_rom_entry(const char *multirom_basepath, const char *rom_name, struct multirom_partition *partition);
struct multirom_rom *multirom_create_internal_entry(const char *multirom_basepath, struct multirom_partition *partition);
struct multirom_rom_android_img *multirom_rom_android_img_parse(const char *rom_basepath);
struct multirom_romdata **multirom_scan_romdata(const char *rom_basepath, enum multirom_rom_type rom_type, int is_internal);
struct multirom_romdata *multirom_parse_romdata_entry(const char *rom_basepath, const char *data_name, enum multirom_rom_type rom_type, int is_internal);
struct multirom_romdata *multirom_create_internal_data_entry(const char *rom_basepath);
struct multirom_romdata_android_img *multirom_romdata_android_img_parse(const char *romdata_basepath);

#endif /* MULTIROM_ROM_H_ */
