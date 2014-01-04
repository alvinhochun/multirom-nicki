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
        multirom_emergency_reboot_recovery();
        while(1);
    }

    // TODO: load and save preferences
    //multirom_load_default_pref();
    multirom_status.pref.brightness = 40;

    multirom_refresh_partitions();
    int parts_count = list_item_count(multirom_status.partitions);
    if(parts_count == 0)
    {
        ERROR("No partitions mounted!");
        multirom_emergency_reboot_recovery();
        while(1);
    }

    int i;
    for(i = 0; i < parts_count; i++)
    {
        struct multirom_rom **roms = multirom_scan_roms(multirom_status.partitions[i]);
        list_add_from_list(roms, &multirom_status.roms);
        free(roms);
    }

    struct multirom_rom *to_boot;
    struct multirom_romdata *boot_profile;
    int ui_exit = multirom_ui(&to_boot, &boot_profile);

    enum exit_status exit = EXIT_REBOOT_RECOVERY;

    switch(ui_exit)
    {
        case UI_EXIT_BOOT_ROM:
            // TODO: prepare for boot
            //exit = multirom_prepare_boot(to_boot);
            if(to_boot->type == ROM_TYPE_ANDROID_INT && boot_profile->type == ROMDATA_TYPE_ANDROID_INT)
                exit = EXIT_NORMALBOOT;
            else
            {
                ERROR("Function not implemented!");
                multirom_emergency_reboot_recovery();
            }
            break;
        case UI_EXIT_REBOOT:
            exit = EXIT_REBOOT;
            break;
        case UI_EXIT_REBOOT_RECOVERY:
//#if MR_COMBINEDROOT
#if 1
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
