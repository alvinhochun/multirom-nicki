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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/klog.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cutils/android_reboot.h>

// clone libbootimg to /system/extras/ from
// https://github.com/Tasssadar/libbootimg.git
#include <libbootimg.h>

#include "multirom_misc.h"
#include "framebuffer.h"
#include "input.h"
#include "log.h"
#include "util.h"

void multirom_emergency_reboot_recovery(void)
{
    if(multirom_init_fb(0) < 0)
    {
        ERROR("Failed to init framebuffer in emergency reboot");
        multirom_copy_log(NULL);
    }
    else
    {
        char *klog = multirom_get_klog();

        fb_add_text(0, 0, WHITE, SIZE_NORMAL,
            "An error occured.\n"
            "Stopping MultiROM!\n"
            "Log file copied to:\n"
            "sdcard/multirom_error.txt\n\n"
            "Press POWER button to\n"
            "reboot to recovery."
        );

        fb_add_text(0, 7 * ISO_CHAR_HEIGHT * SIZE_NORMAL + ISO_CHAR_HEIGHT * SIZE_SMALL, GRAYISH, SIZE_SMALL, "Last lines from klog:");
        fb_add_rect(0, 7 * ISO_CHAR_HEIGHT * SIZE_NORMAL + 2 * ISO_CHAR_HEIGHT * SIZE_SMALL, fb_width, 2, GRAYISH);

        char *tail = klog+strlen(klog);
        int count = (fb_height - (7 * ISO_CHAR_HEIGHT * SIZE_NORMAL + 2 * ISO_CHAR_HEIGHT * SIZE_SMALL + 2)) / (ISO_CHAR_HEIGHT * SIZE_SMALL);
        while(tail > klog && count >= 0)
        {
            --tail;
            if(*tail == '\n')
                --count;
        }

        fb_add_text_long(0, 8 * ISO_CHAR_HEIGHT * SIZE_NORMAL + SIZE_SMALL + 2, GRAYISH, 1, ++tail);

        fb_draw();

        multirom_copy_log(klog);
        free(klog);

        // Wait for power key
        start_input_thread();
        while(wait_for_key() != KEY_POWER);
        stop_input_thread();

        fb_clear();
        fb_close();
    }

    android_reboot(ANDROID_RB_RESTART2, 0, "recovery");

    while(1);
}

int multirom_init_fb(int rotation)
{
    vt_set_mode(1);

    if(fb_open(rotation) < 0)
    {
        ERROR("Failed to open framebuffer!");
        return -1;
    }

    fb_fill(BLACK);
    return 0;
}

int multirom_has_kexec(void)
{
    static int has_kexec = -2;
    if(has_kexec != -2)
        return has_kexec;

    if(access("/proc/config.gz", F_OK) >= 0)
    {
        shell_cmd("/multirom/busybox zcat \"/proc/config.gz\" > \"/config\"");
        has_kexec = 0;

        uint32_t i;
        static const char *checks[] = {
            "CONFIG_KEXEC_HARDBOOT=y",
#ifndef MR_KEXEC_DTB
            "CONFIG_ATAGS_PROC=y",
#else
            "CONFIG_PROC_DEVICETREE=y",
#endif
        };
        //                   0             1       2     3
        char *cmd_grep[] = { "/multirom/busybox", "grep", NULL, "/config", NULL };
        for(i = 0; i < ARRAY_SIZE(checks); ++i)
        {
            cmd_grep[2] = (char*)checks[i];
            if(run_cmd(cmd_grep) != 0)
            {
                has_kexec = -1;
                ERROR("%s not found in /proc/config.gz!\n", checks[i]);
            }
        }

        remove("/config");
    }
    else
    {
        // Kernel without /proc/config.gz enabled - check for /proc/atags file,
        // if it is present, there is good change kexec-hardboot is enabled too.
        ERROR("/proc/config.gz is not available!\n");
#ifndef MR_KEXEC_DTB
        const char *checkfile = "/proc/atags";
#else
        const char *checkfile = "/proc/device-tree";
#endif
        if(access(checkfile, R_OK) < 0)
        {
            ERROR("%s was not found!\n", checkfile);
            has_kexec = -1;
        }
        else
            has_kexec = 0;
    }

    return has_kexec;
}

char *multirom_get_bootloader_cmdline(void)
{
    FILE *f;
    char *c, *e, *l;
    struct boot_img_hdr hdr;
    struct fstab_part *boot;
    char *cmdline;

    f = fopen("/proc/cmdline", "r");
    if(f == NULL)
    {
        ERROR("Cannot open /proc/cmdline");
        return NULL;
    }

    cmdline = malloc(2048);

    if(fgets(cmdline, 2048, f) == NULL)
    {
        ERROR("Cannot read /proc/cmdline");
        free(cmdline);
        cmdline = NULL;
        goto exit;
    }

    size_t len = strlen(cmdline);
    if(cmdline[len - 1] == '\n')
        cmdline[len - 1] = '\0';

    for(c = strchr(cmdline, '\n'); c != NULL; c = strchr(c + 1, '\n'))
        *c = ' ';

    // Remove the part from boot.img
    boot = fstab_find_by_path(multirom_status.fstab, "/boot");
    if(boot && libbootimg_load_header(&hdr, boot->device) >= 0)
    {
        hdr.cmdline[BOOT_ARGS_SIZE - 1] = 0;
        l = (char *)hdr.cmdline;

        if(l[0] != '\0' && (c = strstr(cmdline, l)) != NULL)
        {
            e = c + strlen(l);
            if(e[0] == ' ')
                ++e;
            memmove(c, e, strlen(e) + 1); // plus NULL
        }
    }

exit:
    fclose(f);
    return cmdline;
}

char *multirom_get_klog(void)
{
    int len = klogctl(10, NULL, 0);
    if      (len < 16*1024)      len = 16*1024;
    else if (len > 16*1024*1024) len = 16*1024*1024;

    char *buff = malloc(len);
    klogctl(3, buff, len);
    if(len <= 0)
    {
        ERROR("Could not get klog!\n");
        free(buff);
        return NULL;
    }
    return buff;
}

int multirom_copy_log(char *klog)
{
    int res = 0;
    int freeLog = (klog == NULL);

    if(!klog)
        klog = multirom_get_klog();

    if(klog)
    {
        const char *dir;
        if(multirom_status.external_sd != NULL)
            dir = multirom_status.external_sd;
        else
            dir = "/mnt/internal";

        char path[256];
        sprintf(path, "%s/multirom_error.txt", dir);
        FILE *f = fopen(path, "w");
        if(f)
        {
            fwrite(klog, 1, strlen(klog), f);
            fclose(f);
            chmod(path, 0777);
        }
        else
        {
            ERROR("Failed to open %s!\n", path);
            res = -1;
        }
    }
    else
    {
        ERROR("Could not get klog!\n");
        res = -1;
    }

    if(freeLog)
        free(klog);
    return res;
}

int multirom_get_battery(void)
{
    char buff[4];

    FILE *f = fopen("/sys/class/power_supply/battery/capacity", "r");
    if(!f)
        return -1;

    fgets(buff, sizeof(buff), f);
    fclose(f);

    return atoi(buff);
}

void multirom_set_brightness(int val)
{
#ifdef TW_BRIGHTNESS_PATH
    FILE *f = fopen(TW_BRIGHTNESS_PATH, "w");
    if(!f)
    {
        ERROR("Failed to set brightness: %s!\n", strerror(errno));
        return;
    }
    fprintf(f, "%d", val);
    fclose(f);
#endif
}

void multirom_take_screenshot(void)
{
    char *buffer = NULL;
    int len = fb_clone(&buffer);

    int counter;
    char path[256];
    struct stat info;
    FILE *f = NULL;

    for(counter = 0; 1; ++counter)
    {
        const char *dir;
        if(multirom_status.external_sd != NULL)
            dir = multirom_status.external_sd;
        else
            dir = "/mnt/internal";

        sprintf(path, "%s/multirom_screenshot_%02d.raw", dir, counter);
        if(stat(path, &info) >= 0)
            continue;

        f = fopen(path, "w");
        if(f)
        {
            fwrite(buffer, 1, len, f);
            fclose(f);
        }
        break;
    }

    free(buffer);

    fb_fill(WHITE);
    fb_update();
    usleep(100000);
    fb_draw();
}

int multirom_get_trampoline_ver(void)
{
    static int ver = -2;
    if(ver == -2)
    {
        ver = -1;

        char *const cmd[] = { "/init", "-v", NULL };
        char *res = run_get_stdout(cmd);
        if(res)
        {
            ver = atoi(res);
            free(res);
        }
    }
    return ver;
}
