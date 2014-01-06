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

#ifndef MULTIROM_MISC_H_
#define MULTIROM_MISC_H_

#include "multirom_rom.h"
#include "multirom_status.h"

void multirom_emergency_reboot(void) __attribute__((noreturn));
int multirom_init_fb(int rotation);
int multirom_has_kexec(void);
char *multirom_get_bootloader_cmdline(void);
int multirom_load_kexec(struct multirom_status *s, struct multirom_rom *rom);
char *multirom_get_klog(void);
int multirom_copy_log(char *klog);
int multirom_get_battery(void);
void multirom_set_brightness(int val);
void multirom_take_screenshot(void);
int multirom_get_trampoline_ver(void);

#endif /* MULTIROM_MISC_H_ */
