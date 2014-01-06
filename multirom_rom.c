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
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

#include "multirom_rom.h"
#include "util.h"
#include "log.h"

/*
 * Scans for ROMs.
 * Also use this even if internal cannot be mounted (e.g. encrypted) to create internal entry,
 * but if this is the case, pass a part for a temp dir or this may blow up.
 * `partition`: the partition to scan (or a dummy one to a temp dir)
 * returns: a list (ptr to ptr) of struct multirom_rom
 */
struct multirom_rom **multirom_scan_roms(struct multirom_partition *partition)
{
    INFO("Scanning for roms...");
    char *multirom_basepath;
    int res;
    if(partition->type == PART_INTERNAL)
        res = asprintf(&multirom_basepath, "%s/multirom", partition->mount_path);
    else
        res = asprintf(&multirom_basepath, "%s/multirom-"TARGET_DEVICE, partition->mount_path);
    if(res < 0)
        goto fail;

    if(access(multirom_basepath, F_OK) < 0)
    {
        if(mkdir(multirom_basepath, 0755) < 0)
        {
            ERROR("MultiROM directory %s does not exist and cannot be created!", multirom_basepath);
            goto fail;
        }
    }

    struct multirom_rom **roms = NULL;
    int has_internal = 0;

    DIR *d = opendir(multirom_basepath);
    if(!d)
    {
        ERROR("Cannot open MultiROM directory %s!", multirom_basepath);
        goto fail;
    }

    struct dirent *dr;
    while((dr = readdir(d)) != NULL)
    {
        if(dr->d_name[0] == '.')
            continue;

        struct multirom_rom *rom = multirom_parse_rom_entry(multirom_basepath, dr->d_name, partition);
        if(rom == NULL)
            continue;

        if(ROMTYPE_FMT(rom->type) == ROMTYPE_FMT_INT)
            has_internal = 1;
        list_add(rom, &roms);
    }
    closedir(d);

    // Add internal
    if(partition->type == PART_INTERNAL && !has_internal)
    {
        ERROR("Internal ROM entry not found, attempting to create it...");
        struct multirom_rom *rom = multirom_create_internal_entry(multirom_basepath, partition);
        if(rom == NULL)
        {
            ERROR("Internal ROM entry does not exist and MultiROM cannot create it!");
            goto fail;
        }
        list_add(rom, &roms);
    }

    goto done;

fail:
    list_clear(&roms, free_multirom_rom);

done:
    free(multirom_basepath);
    return roms;
}

/*
 * Parse a ROM and return its information
 * `multirom_basepath`: the path to the multirom dir of the partition
 * `rom_name`: the rom (dir) name
 * `partition`: the partition where this rom is in
 *  returns: a pointer to a struct multirom_rom or NULL
 */
struct multirom_rom *multirom_parse_rom_entry(const char *multirom_basepath, const char *rom_name, struct multirom_partition *partition)
{
    struct multirom_rom *rom = NULL;
    char *rom_basepath = NULL;
    int res = asprintf(&rom_basepath, "%s/%s", multirom_basepath, rom_name);
    if(res < 0)
        goto fail;

    rom = mzalloc(sizeof(struct multirom_rom));
    if(rom == NULL)
        goto fail;

    rom->name = strdup(rom_name);
    rom->partition = partition;

    if(strcmp(rom_name, "internal") == 0)
    {
        rom->type = ROM_TYPE_ANDROID_INT;
    }
    else
    {
        char buf[256];
        res = snprintf(buf, sizeof(buf), "%s/rom.cfg", rom_basepath);
        if(res >= (int)sizeof(buf) || res < 0)
            goto fail;

        FILE *f = fopen(buf, "r");
        if(f == NULL)
        {
            ERROR("Failed to open ROM config \"%s\"", buf);
            goto fail;
        }

        char line[1024];
        char key[256];
        char value[256];
        char *pch;

        while((fgets(line, sizeof(line), f)))
        {
            if(line[0] == '#')
                continue;

            pch = strtok(line, "=\n");
            if(pch == NULL) continue;
            strncpy(key, pch, sizeof(key));
            pch = strtok(NULL, "\n");
            if(pch == NULL) continue;
            strncpy(value, pch, sizeof(value));

            if(strcmp(key, "type") == 0)
            {
                if(strcmp(value, "android_img") == 0)
                {
                    rom->type = ROM_TYPE_ANDROID_IMG;
                }
                else
                {
                    value[sizeof(value) - 1] = '\0'; // strncpy does not null-terminate!
                    ERROR("Unknown ROM type %s for %s!", value, rom_basepath);
                    fclose(f);
                    goto fail;
                }
            }
        }

        fclose(f);
    }

    switch(rom->type)
    {
    case ROM_TYPE_ANDROID_IMG:
        rom->android_img = multirom_rom_android_img_parse(rom_basepath);
        if(rom->android_img == NULL)
        {
            ERROR("Cannot get ROM system images for %s!", rom_basepath);
            goto fail;
        }
        break;
    case ROM_TYPE_ANDROID_INT:
        break;
    default:
        ERROR("Invalid ROM type for %s!", rom_basepath);
        goto fail;
    }

    rom->romdata_list = multirom_scan_romdata(rom_basepath, rom->type, partition->type == PART_INTERNAL);
    if(list_item_count(rom->romdata_list) <= 0)
    {
        ERROR("No ROM profiles for %s!", rom_basepath);
        goto fail;
    }

    goto done;

fail:
    if(rom != NULL)
    {
        free_multirom_rom(rom);
        rom = NULL;
    }

done:
    if(rom_basepath != NULL)
        free(rom_basepath);

    return rom;
}

/*
 * Creates internal ROM entry 'cause it does not exist
 * Also use this even if internal cannot be mounted (e.g. encrypted),
 * but if this is the case, pass a temp directory or this may blow up.
 * `multirom_basepath`: the path to the multirom dir of the partition or temp dir
 * `partition`: the partition (or a dummy one for temp dir)
 */
struct multirom_rom *multirom_create_internal_entry(const char *multirom_basepath, struct multirom_partition *partition)
{
    struct multirom_rom *rom = NULL;
    char *rom_basepath = NULL;
    int res = asprintf(&rom_basepath, "%s/internal", multirom_basepath);
    if(res < 0)
        goto fail;

    rom = mzalloc(sizeof(struct multirom_rom));
    if(rom == NULL)
        goto fail;

    if(mkdir(rom_basepath, 0755) < 0)
    {
        ERROR("Cannot create directory %s!", rom_basepath);
        ERROR("Continuing since this is not critical");
    }

    rom->name = strdup("internal"); // should be heap memory
    rom->partition = partition;
    rom->type = ROM_TYPE_ANDROID_INT;
    struct multirom_romdata *romdata = multirom_create_internal_data_entry(rom_basepath);
    if(romdata == NULL)
        goto fail;
    list_add(romdata, &rom->romdata_list);

    goto done;

fail:
    if(rom != NULL)
    {
        free_multirom_rom(rom);
        rom = NULL;
    }

done:
    if(rom_basepath != NULL)
        free(rom_basepath);

    return rom;
}

struct multirom_rom_android_img *multirom_rom_android_img_parse(const char *romdata_basepath)
{
    char *imgpath = NULL;
    int res;
    struct multirom_rom_android_img *data = mzalloc(sizeof(struct multirom_rom_android_img));
    if(data == NULL)
        goto fail;

    res = asprintf(&imgpath, "%s/kernel", romdata_basepath);
    if(res < 0)
        goto fail;

    if(access(imgpath, F_OK) >= 0)
    {
        ERROR("%s found", imgpath);
        if(access(imgpath, R_OK) < 0)
        {
            ERROR("Cannot access %s! Error: %d %s", imgpath, errno, strerror(errno));
            goto fail;
        }
        data->kernel_path = imgpath;
        imgpath = NULL;

        res = asprintf(&imgpath, "%s/cmdline", romdata_basepath);
        if(res < 0)
            goto fail;
        FILE *f = fopen(imgpath, "r");
        if(f == NULL)
        {
            ERROR("Cannot read %s!", imgpath);
            goto fail;
        }
        fseek(f, 0, SEEK_END);
        size_t size = ftell(f);
        rewind(f);
        data->cmdline = malloc(size + 2);
        data->cmdline[0] = '\0';
        fgets(data->cmdline, size + 1, f);
        fclose(f);
        size_t l = strlen(data->cmdline);
        if(l != 0)
        {
            if(data->cmdline[l - 1] == '\n')
            {
                data->cmdline[l - 1] = ' ';
            }
            else
            {
                data->cmdline[l] = ' ';
                data->cmdline[l + 1] = '\0';
            }
        }
    }

    res = asprintf(&imgpath, "%s/ramdisk.gz", romdata_basepath);
    if(res < 0)
        goto fail;

    if(access(imgpath, R_OK) < 0)
    {
        ERROR("Cannot access %s! Error: %d %s", imgpath, errno, strerror(errno));
        goto fail;
    }

    data->ramdisk_path = imgpath;
    imgpath = NULL;

    res = asprintf(&imgpath, "%s/system.img", romdata_basepath);
    if(res < 0)
        goto fail;

    if(access(imgpath, R_OK | W_OK) < 0)
    {
        ERROR("Cannot access %s! Error: %d %s", imgpath, errno, strerror(errno));
        goto fail;
    }

    data->system_path = imgpath;
    imgpath = NULL;

    res = asprintf(&imgpath, "%s/firmware.img", romdata_basepath);
    if(res < 0)
        goto fail;

    if(access(imgpath, F_OK) >= 0)
    {
        ERROR("%s found", imgpath);
        if(access(imgpath, R_OK) < 0)
        {
            ERROR("Cannot access %s! Error: %d %s", imgpath, errno, strerror(errno));
            goto fail;
        }
        data->firmware_path = imgpath;
        imgpath = NULL;
    }

    goto done;

fail:
    if(data != NULL)
    {
        free_multirom_rom_android_img(data);
        data = NULL;
    }

done:
    if(imgpath != NULL)
        free(imgpath);

    return data;
}

struct multirom_romdata **multirom_scan_romdata(const char *rom_basepath, enum multirom_rom_type rom_type, int is_internal)
{
    int res;
    struct multirom_romdata **romdata_list = NULL;
    int has_internal = 0;

    DIR *d = opendir(rom_basepath);
    if(!d)
    {
        ERROR("Cannot open ROM directory %s!", rom_basepath);
        goto fail;
    }

    struct dirent *dr;
    while((dr = readdir(d)) != NULL)
    {
        if(dr->d_name[0] == '.')
            continue;

        if(dr->d_type != DT_DIR)
            continue;

        struct multirom_romdata *romdata = multirom_parse_romdata_entry(rom_basepath, dr->d_name, rom_type, is_internal);
        if(romdata == NULL)
            continue;

        if(ROMTYPE_FMT(romdata->type) == ROMTYPE_FMT_INT)
            has_internal = 1;
        list_add(romdata, &romdata_list);
    }
    closedir(d);

    // Add internal
    if(is_internal && !has_internal)
    {
        ERROR("Internal ROM internal profile entry not found, attempting to create it...");
        struct multirom_romdata *romdata = multirom_create_internal_data_entry(rom_basepath);
        if(romdata == NULL)
        {
            ERROR("Internal ROM internal profile entry does not exist and MultiROM cannot create it!");
            goto fail;
        }
        list_add(romdata, &romdata_list);
    }

    goto done;

fail:
    list_clear(&romdata_list, free_multirom_romdata);

done:

    return romdata_list;
}

struct multirom_romdata *multirom_parse_romdata_entry(const char *rom_basepath, const char *data_name, enum multirom_rom_type rom_type, int is_internal)
{
    struct multirom_romdata *romdata = NULL;
    char *romdata_basepath = NULL;
    int res = asprintf(&romdata_basepath, "%s/%s", rom_basepath, data_name);
    if(res < 0)
        goto fail;

    romdata = mzalloc(sizeof(struct multirom_romdata));
    if(romdata == NULL)
        goto fail;

    romdata->name = strdup(data_name);

    if(ROMTYPE_FMT(rom_type) == ROMTYPE_FMT_INT && strcmp(data_name, "internal") == 0)
    {
        if(is_internal)
            romdata->type = ROMDATA_TYPE_ANDROID_INT;
        else
        {
            ERROR("\"internal\" profile on %s is ignored", rom_basepath);
            goto fail;
        }
    }
    else
    {
        char buf[256];
        res = snprintf(buf, sizeof(buf), "%s/profile.cfg", romdata_basepath);
        if(res >= (int)sizeof(buf) || res < 0)
            goto fail;

        FILE *f = fopen(buf, "r");
        if(f == NULL)
        {
            ERROR("Failed to open ROM profile config \"%s\"", buf);
            goto fail;
        }

        char line[1024];
        char key[256];
        char value[256];
        char *pch;

        while((fgets(line, sizeof(line), f)))
        {
            if(line[0] == '#')
                continue;

            pch = strtok(line, "=\n");
            if(pch == NULL) continue;
            strncpy(key, pch, sizeof(key));
            pch = strtok(NULL, "\n");
            if(pch == NULL) continue;
            strncpy(value, pch, sizeof(value));

            if(strcmp(key, "type") == 0)
            {
                if(strcmp(value, "android_img") == 0)
                {
                    romdata->type = ROMDATA_TYPE_ANDROID_IMG;
                }
                else
                {
                    value[sizeof(value) - 1] = '\0'; // strncpy does not null-terminate!
                    ERROR("Unknown ROM profile type %s for %s!", value, romdata_basepath);
                    fclose(f);
                    goto fail;
                }
            }
        }

        fclose(f);
    }

    switch(romdata->type)
    {
    case ROMDATA_TYPE_ANDROID_IMG:
        romdata->android_img = multirom_romdata_android_img_parse(romdata_basepath);
        if(romdata->android_img == NULL)
        {
            ERROR("Cannot get ROM profile images for %s!", romdata_basepath);
            goto fail;
        }
        break;
    case ROMDATA_TYPE_ANDROID_INT:
        break;
    default:
        ERROR("Invalid ROM profile type for %s!", romdata_basepath);
        goto fail;
    }

    goto done;

fail:
    if(romdata != NULL)
    {
        free_multirom_romdata(romdata);
        romdata = NULL;
    }

done:
    if(romdata_basepath != NULL)
        free(romdata_basepath);

    return romdata;
}

struct multirom_romdata *multirom_create_internal_data_entry(const char *rom_basepath)
{
    struct multirom_romdata *romdata = NULL;
    char *romdata_basepath = NULL;
    int res = asprintf(&romdata_basepath, "%s/internal", rom_basepath);
    if(res < 0)
        goto fail;

    romdata = mzalloc(sizeof(struct multirom_romdata));
    if(romdata == NULL)
        goto fail;

    if(mkdir(romdata_basepath, 0755) < 0)
    {
        ERROR("Cannot create directory %s!", rom_basepath);
        ERROR("Continuing since this is not critical");
    }

    romdata->name = strdup("internal"); // should be heap memory
    romdata->type = ROMDATA_TYPE_ANDROID_INT;

    goto done;

fail:
    if(romdata != NULL)
    {
        free_multirom_romdata(romdata);
        romdata = NULL;
    }

done:
    if(romdata_basepath != NULL)
        free(romdata_basepath);

    return romdata;
}

struct multirom_romdata_android_img *multirom_romdata_android_img_parse(const char *romdata_basepath)
{
    char *imgpath = NULL;
    int res;
    struct multirom_romdata_android_img *data = mzalloc(sizeof(struct multirom_romdata_android_img));
    if(data == NULL)
        goto fail;

    res = asprintf(&imgpath, "%s/data.img", romdata_basepath);
    if(res < 0)
        goto fail;

    if(access(imgpath, R_OK | W_OK) < 0)
    {
        ERROR("Cannot access %s! Error: %d %s", imgpath, errno, strerror(errno));
        goto fail;
    }

    data->data_path = imgpath;
    imgpath = NULL;

    res = asprintf(&imgpath, "%s/cache.img", romdata_basepath);
    if(res < 0)
        goto fail;

    if(access(imgpath, R_OK | W_OK) < 0)
    {
        ERROR("Cannot access %s! Error: %d %s", imgpath, errno, strerror(errno));
        goto fail;
    }

    data->cache_path = imgpath;
    imgpath = NULL;

    res = asprintf(&imgpath, "%s/persist.img", romdata_basepath);
    if(res < 0)
        goto fail;

    if(access(imgpath, R_OK | W_OK) < 0)
    {
        ERROR("Cannot access %s! Error: %d %s", imgpath, errno, strerror(errno));
        goto fail;
    }

    data->persist_path = imgpath;
    imgpath = NULL;

    goto done;

fail:
    if(data != NULL)
    {
        free_multirom_romdata_android_img(data);
        data = NULL;
    }

done:
    if(imgpath != NULL)
        free(imgpath);

    return data;
}
