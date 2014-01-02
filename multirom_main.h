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

#ifndef MULTIROM_MAIN_H_
#define MULTIROM_MAIN_H_

enum exit_status
{
    EXIT_NORMALBOOT          = 0,
    EXIT_REBOOT              = 0x01,
    //EXIT_UMOUNT              = 0x02,
    EXIT_REBOOT_RECOVERY     = 0x04,
    EXIT_REBOOT_BOOTLOADER   = 0x08,
    EXIT_POWEROFF            = 0x10,
    EXIT_KEXEC               = 0x20,

    EXIT_REBOOT_MASK         = (EXIT_REBOOT | EXIT_REBOOT_RECOVERY | EXIT_REBOOT_BOOTLOADER | EXIT_POWEROFF),
};

enum exit_status multirom(void);

#endif /* MULTIROM_MAIN_H_ */
