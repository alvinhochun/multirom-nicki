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

#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>

#include "multirom_ui.h"
#include "framebuffer.h"
#include "input.h"
#include "log.h"
#include "listview.h"
#include "util.h"
#include "button.h"
#include "checkbox.h"
#include "version.h"
#include "pong.h"
#include "progressdots.h"
#include "multirom_ui_themes.h"
#include "workers.h"
#include "hooks.h"


//static struct multirom_status *mrom_status = NULL;
static struct multirom_rom *selected_rom = NULL;
static struct multirom_romdata *selected_profile = NULL;
static volatile int exit_ui_code = -1;
static volatile fb_msgbox *active_msgbox = NULL;
static volatile int loop_act = 0;
static multirom_themes_info *themes_info = NULL;
static multirom_theme *cur_theme = NULL;

static pthread_mutex_t exit_code_mutex = PTHREAD_MUTEX_INITIALIZER;

uint32_t CLR_PRIMARY = LBLUE;
uint32_t CLR_SECONDARY = LBLUE2;

//#define LOOP_UPDATE_USB 0x01
#define LOOP_EXT_RESCAN 0x01
#define LOOP_START_PONG 0x02
#define LOOP_CHANGE_CLR 0x04

int multirom_ui(struct multirom_rom **to_boot, struct multirom_romdata **boot_profile)
{
    if(multirom_init_fb(0) < 0)
        return UI_EXIT_BOOT_ROM;

    fb_freeze(1);

    exit_ui_code = -1;
    selected_rom = NULL;
    selected_profile = NULL;
    active_msgbox = NULL;

    multirom_ui_setup_colors(multirom_status.pref.color, &CLR_PRIMARY, &CLR_SECONDARY);
    themes_info = multirom_ui_init_themes();
    if((cur_theme = multirom_ui_select_theme(themes_info, fb_width, fb_height)) == NULL)
    {
        fb_freeze(0);

        ERROR("Couldn't find theme for resolution %dx%d!\n", fb_width, fb_height);
        fb_add_text(0, 0, WHITE, SIZE_SMALL, "Couldn't find theme for\nresolution %dx%d!\nPress POWER to reboot.", fb_width, fb_height);
        fb_draw();
        fb_clear();
        fb_close();

        start_input_thread();
        while(wait_for_key() != KEY_POWER);
        stop_input_thread();
        return UI_EXIT_REBOOT;
    }

    workers_start();

    multirom_ui_init_header();
    multirom_ui_switch(TAB_INTERNAL);

    add_touch_handler(&multirom_ui_touch_handler, NULL);
    start_input_thread();
    keyaction_enable(1);
    keyaction_set_destroy_msgbox_handle(multirom_ui_destroy_msgbox);

    multirom_set_brightness(multirom_status.pref.brightness);

    fb_freeze(0);

    // TODO: Add auto boot function
    /*if(s->auto_boot_rom && s->auto_boot_seconds > 0)
        multirom_ui_auto_boot();
    else*/
        fb_draw();

    while(1)
    {
        pthread_mutex_lock(&exit_code_mutex);
        if(exit_ui_code != -1)
        {
            pthread_mutex_unlock(&exit_code_mutex);
            break;
        }

        // TODO: USB hot plugging
        /*if(loop_act & LOOP_UPDATE_USB)
        {
            multirom_find_usb_roms(mrom_status);
            if(themes_info->data->selected_tab == TAB_USB)
                multirom_ui_tab_rom_update_usb(themes_info->data->tab_data);
            loop_act &= ~(LOOP_UPDATE_USB);
        }*/

        if(loop_act & LOOP_EXT_RESCAN)
        {
            list_clear(&multirom_status.roms, free_multirom_rom);
            multirom_clear_partitions();

            active_msgbox = fb_create_msgbox(416*DPI_MUL, 360*DPI_MUL, CLR_PRIMARY);
            fb_msgbox_add_text(-1, 30*DPI_MUL, SIZE_BIG, "Storage Devices");
            fb_msgbox_add_text(-1, 80*DPI_MUL, SIZE_BIG, "Unmounted");
            fb_msgbox_add_text(-1, 140*DPI_MUL, SIZE_NORMAL, "You can now remove and");
            fb_msgbox_add_text(-1, 175*DPI_MUL, SIZE_NORMAL, "replace the SD card or");
            fb_msgbox_add_text(-1, 210*DPI_MUL, SIZE_NORMAL, "USB storage.");
            fb_msgbox_add_text(-1, active_msgbox->h-60*DPI_MUL, SIZE_NORMAL, "Touch to rescan ROMs");

            fb_draw();
            fb_freeze(1);
            set_touch_handlers_mode(HANDLERS_ALL);

            int msgbox_visible = 1;
            do
            {
                pthread_mutex_unlock(&exit_code_mutex);
                usleep(100000);
                pthread_mutex_lock(&exit_code_mutex);
                msgbox_visible = active_msgbox != NULL;
            } while(msgbox_visible);

            multirom_scan_partitions();
            multirom_scan_all_roms();

            loop_act &= ~(LOOP_EXT_RESCAN);
        }

        if(loop_act & LOOP_START_PONG)
        {
            loop_act &= ~(LOOP_START_PONG);
            keyaction_enable(0);
            input_push_context();
            fb_push_context();

            pong();

            fb_pop_context();
            input_pop_context();
            keyaction_enable(1);
        }

        if(loop_act & LOOP_CHANGE_CLR)
        {
            fb_freeze(1);

            multirom_ui_setup_colors(multirom_status.pref.color, &CLR_PRIMARY, &CLR_SECONDARY);

            // force redraw tab
            int tab = themes_info->data->selected_tab;
            themes_info->data->selected_tab = -1;

            multirom_ui_destroy_tab(tab);
            multirom_ui_switch(tab);

            fb_freeze(0);
            fb_draw();

            loop_act &= ~(LOOP_CHANGE_CLR);
        }

        pthread_mutex_unlock(&exit_code_mutex);

        usleep(100000);
    }

    keyaction_enable(0);
    keyaction_clear();

    rm_touch_handler(&multirom_ui_touch_handler, NULL);

    fb_create_msgbox(416*DPI_MUL, 250*DPI_MUL, CLR_PRIMARY);

    switch(exit_ui_code)
    {
        case UI_EXIT_BOOT_ROM:
            *to_boot = selected_rom;
            *boot_profile = selected_profile;
            fb_msgbox_add_text(-1, 40*DPI_MUL, SIZE_BIG, "Booting ROM...");
            fb_msgbox_add_text(-1, 40*DPI_MUL + ISO_CHAR_HEIGHT * SIZE_BIG * 2, SIZE_NORMAL, selected_rom->name);
            fb_msgbox_add_text(-1, 40*DPI_MUL + ISO_CHAR_HEIGHT * (SIZE_BIG * 2 + SIZE_NORMAL), SIZE_NORMAL, selected_profile->name);
            break;
        case UI_EXIT_REBOOT:
            fb_msgbox_add_text(-1, -1, SIZE_BIG, "Rebooting...");
            break;
        case UI_EXIT_REBOOT_RECOVERY:
            fb_msgbox_add_text(-1, -1, SIZE_BIG, "Recovery...");
            break;
        case UI_EXIT_REBOOT_BOOTLOADER:
            fb_msgbox_add_text(-1, -1, SIZE_BIG, "Bootloader...");
            break;
        case UI_EXIT_POWEROFF:
            fb_msgbox_add_text(-1, -1, SIZE_BIG, "Shutting down...");
            break;
    }

    fb_draw();
    fb_freeze(1);

    cur_theme->destroy(themes_info->data);

    int i;
    for(i = 0; i < TAB_COUNT; ++i)
    {
        button_destroy(themes_info->data->tab_btns[i]);
        themes_info->data->tab_btns[i] = NULL;
    }

    stop_input_thread();

    multirom_ui_destroy_tab(themes_info->data->selected_tab);
    multirom_ui_free_themes(themes_info);
    themes_info = NULL;

    workers_stop();

    fb_clear();
#if MR_DEVICE_HOOKS >= 2
    mrom_hook_before_fb_close();
#endif
    fb_close();
    return exit_ui_code;
}


void multirom_ui_setup_colors(int clr, uint32_t *primary, uint32_t *secondary)
{
    static const int clrs[][2] = {
        // Primary,   Secondary - OxAAGGBBRR
        { LBLUE,      LBLUE2 },     // CLRS_BLUE
        { 0xFFCC66AA, 0xFFCC89B6 }, // CLRS_PURPLE
        { 0xFF00BD8A, 0xFF51F2C9 }, // CLRS_GREEN
        { 0xFF008AFF, 0xFF51AEFF }, // CLRS_ORANGE
        { 0xFF0000CC, 0xFF6363FF }, // CLRS_RED
        { 0xFF2F5EB8, 0xFF689CFF }, // CLRS_BROWN
    };

    if(clr < 0 || clr >= (int)ARRAY_SIZE(clrs))
        clr = 0;

    *primary = clrs[clr][0];
    *secondary = clrs[clr][1];
}

void multirom_ui_init_header(void)
{
    cur_theme->init_header(themes_info->data);
}

void multirom_ui_header_select(int tab)
{
    cur_theme->header_select(themes_info->data, tab);
}

void multirom_ui_destroy_tab(int tab)
{
    switch(tab)
    {
        case -1:
            break;
        case TAB_USB:
        case TAB_INTERNAL:
        case TAB_EXT_SD:
            multirom_ui_tab_rom_destroy(themes_info->data->tab_data);
            break;
        case TAB_MISC:
            multirom_ui_tab_misc_destroy(themes_info->data->tab_data);
            break;
        default:
            assert(0);
            break;
    }
    themes_info->data->tab_data = NULL;
}

void multirom_ui_switch(int tab)
{
    if(tab == themes_info->data->selected_tab)
        return;

    fb_freeze(1);

    multirom_ui_header_select(tab);

    // destroy old tab
    multirom_ui_destroy_tab(themes_info->data->selected_tab);

    // init new tab
    switch(tab)
    {
        case TAB_USB:
        case TAB_INTERNAL:
        case TAB_EXT_SD:
            themes_info->data->tab_data = multirom_ui_tab_rom_init(tab);
            break;
        case TAB_MISC:
            themes_info->data->tab_data = multirom_ui_tab_misc_init();
            break;
    }

    themes_info->data->selected_tab = tab;

    fb_freeze(0);
    fb_draw();
}

void multirom_ui_fill_rom_list(listview *view, enum multirom_partition_type part_type)
{
    int i;
    struct multirom_rom *rom;
    rom_item_data *data;
    listview_item *it;
    char part_desc[64];
    for(i = 0; multirom_status.roms && multirom_status.roms[i]; ++i)
    {
        rom = multirom_status.roms[i];

        if(rom->partition->type != part_type)
            continue;

        sprintf(part_desc, "%s (%s)", rom->partition->name, rom->partition->fstype);

        // TODO: Support hiding internal...?
        //if(rom->type == ROM_DEFAULT && mrom_status->hide_internal)
        //    continue;
        struct multirom_romdata **profile;
        for(profile = rom->romdata_list; profile != NULL && *profile != NULL; profile++)
        {
            data = rom_item_create(rom->name, (*profile)->name, part_desc);
            it = listview_add_item(view, rom, *profile, data);
        }

        // TODO: autoboot support
        /*if ((mrom_status->auto_boot_rom && rom == mrom_status->auto_boot_rom) ||
            (!mrom_status->auto_boot_rom && rom == mrom_status->current_rom))
        {
            listview_select_item(view, it);
        }*/
    }

    if(view->items != NULL && view->selected == NULL)
        listview_select_item(view, view->items[0]);
}

int multirom_ui_touch_handler(touch_event *ev, void *data)
{
    static int touch_count = 0;
    if(ev->changed & TCHNG_ADDED)
    {
        if(++touch_count == 4)
        {
            multirom_take_screenshot();
            touch_count = 0;
        }

        multirom_ui_destroy_msgbox();
    }

    if((ev->changed & TCHNG_REMOVED) && touch_count > 0)
        --touch_count;

    return -1;
}

int multirom_ui_destroy_msgbox(void)
{
    if(!active_msgbox)
        return 0;

    pthread_mutex_lock(&exit_code_mutex);
    fb_destroy_msgbox();
    fb_freeze(0);
    fb_draw();
    active_msgbox = NULL;
    set_touch_handlers_mode(HANDLERS_FIRST);
    pthread_mutex_unlock(&exit_code_mutex);
    return 1;
}

void multirom_ui_auto_boot(void)
{
    // TODO: autoboot support
#if 0
    int seconds = mrom_status->auto_boot_seconds*1000;
    active_msgbox = fb_create_msgbox(416*DPI_MUL, 300*DPI_MUL, CLR_PRIMARY);

    fb_msgbox_add_text(-1, 40*DPI_MUL, SIZE_BIG, "Auto-boot");
    fb_msgbox_add_text(-1, active_msgbox->h-100*DPI_MUL, SIZE_NORMAL, "%s", mrom_status->auto_boot_rom->name);
    fb_msgbox_add_text(-1, active_msgbox->h-60*DPI_MUL, SIZE_NORMAL, "Touch to cancel");

    fb_text *sec_text = fb_msgbox_add_text(-1, -1, SIZE_BIG, "%d", seconds/1000);

    fb_draw();
    fb_freeze(1);
    set_touch_handlers_mode(HANDLERS_ALL);

    while(1)
    {
        pthread_mutex_lock(&exit_code_mutex);
        if(!active_msgbox)
        {
            pthread_mutex_unlock(&exit_code_mutex);
            break;
        }
        pthread_mutex_unlock(&exit_code_mutex);

        seconds -= 50;
        if(seconds <= 0)
        {
            pthread_mutex_lock(&exit_code_mutex);
            selected_rom = mrom_status->auto_boot_rom;
            active_msgbox = NULL;
            exit_ui_code = UI_EXIT_BOOT_ROM;
            pthread_mutex_unlock(&exit_code_mutex);
            fb_destroy_msgbox();
            fb_freeze(0);
            break;
        }
        else if((seconds+50)/1000 != seconds/1000)
        {
            sprintf(sec_text->text, "%d", seconds/1000);
            fb_freeze(0);
            fb_draw();
            fb_freeze(1);
        }
        usleep(50000);
    }
    set_touch_handlers_mode(HANDLERS_FIRST);
#endif
}

void multirom_ui_refresh_usb_handler(void)
{
    // TODO: usb hot plugging
#if 0
    pthread_mutex_lock(&exit_code_mutex);
    loop_act |= LOOP_UPDATE_USB;
    pthread_mutex_unlock(&exit_code_mutex);
#endif
}

void multirom_ui_start_pong(int action)
{
    pthread_mutex_lock(&exit_code_mutex);
    loop_act |= LOOP_START_PONG;
    pthread_mutex_unlock(&exit_code_mutex);
}

void *multirom_ui_tab_rom_init(int tab_type)
{
    tab_data_roms *t = mzalloc(sizeof(tab_data_roms));
    themes_info->data->tab_data = t;

    t->list = mzalloc(sizeof(listview));
    t->list->item_draw = &rom_item_draw;
    t->list->item_hide = &rom_item_hide;
    t->list->item_height = &rom_item_height;
    t->list->item_destroy = &rom_item_destroy;
    t->list->item_selected = &multirom_ui_tab_rom_selected;
    t->list->item_confirmed = &multirom_ui_tab_rom_confirmed;

    t->boot_btn = mzalloc(sizeof(button));
    list_add(t->boot_btn, &t->buttons);

    cur_theme->tab_rom_init(themes_info->data, t, tab_type);

    listview_init_ui(t->list);

    switch(tab_type)
    {
    case TAB_INTERNAL:
        multirom_ui_fill_rom_list(t->list, PART_INTERNAL);
        break;
    case TAB_EXT_SD:
        multirom_ui_fill_rom_list(t->list, PART_EXTERNAL_SD);
        break;
    case TAB_USB:
        // TODO: usb hot plugging
        multirom_ui_fill_rom_list(t->list, PART_EXTERNAL_USBDISK);
        break;
    }

    listview_update_ui(t->list);

    int has_roms = (int)(t->list->items == NULL);
    multirom_ui_tab_rom_set_empty(t, has_roms, tab_type);

    t->boot_btn->clicked = &multirom_ui_tab_rom_boot_btn;
    button_init_ui(t->boot_btn, "Boot", SIZE_BIG);
    button_enable(t->boot_btn, !has_roms);

    // TODO: usb hot plugging
#if 0
    if(tab_type == TAB_USB)
    {
        multirom_set_usb_refresh_handler(&multirom_ui_refresh_usb_handler);
        multirom_set_usb_refresh_thread(mrom_status, 1);
    }
#endif
    return t;
}

void multirom_ui_tab_rom_destroy(tab_data_roms *data)
{
    // TODO: usb hot plugging
    //multirom_set_usb_refresh_thread(mrom_status, 0);

    tab_data_roms *t = data;

    list_clear(&t->buttons, &button_destroy);
    list_clear(&t->ui_elements, &fb_remove_item);

    listview_destroy(t->list);

    fb_rm_text(t->rom_name);
    fb_rm_text(t->rom_profile);

    if(t->usb_prog)
        progdots_destroy(t->usb_prog);

    free(t);
}

void multirom_ui_tab_rom_selected(listview_item *prev, listview_item *now)
{
    struct multirom_rom *rom = now->rom;
    struct multirom_romdata *profile = now->profile;
    if(rom == NULL || profile == NULL || themes_info->data->tab_data == NULL)
        return;

    tab_data_roms *t = (tab_data_roms*)themes_info->data->tab_data;

    free(t->rom_name->text);
    t->rom_name->text = strdup(rom->name);

    free(t->rom_profile->text);
    size_t profile_len = strlen(profile->name);
    t->rom_profile->text = malloc(profile_len + 3);
    t->rom_profile->text[0] = '<';
    strcpy(t->rom_profile->text + 1, profile->name);
    t->rom_profile->text[profile_len + 1] = '>';
    t->rom_profile->text[profile_len + 2] = '\0';

    cur_theme->center_rom_name(t, rom->name, t->rom_profile->text);

    fb_draw();
}

void multirom_ui_tab_rom_confirmed(listview_item *it)
{
    multirom_ui_tab_rom_boot_btn(0);
}

void multirom_ui_tab_rom_boot_btn(int action)
{
    if(!themes_info->data->tab_data)
        return;

    tab_data_roms *t = (tab_data_roms*)themes_info->data->tab_data;
    if(!t->list->selected)
        return;

    struct multirom_rom *rom = t->list->selected->rom;
    struct multirom_romdata *profile = t->list->selected->profile;
    if(rom == NULL || profile == NULL)
        return;

#if 0
    int m = M(rom->type);
    if(m & MASK_UNSUPPORTED)
    {
        active_msgbox = fb_create_msgbox(416*DPI_MUL, 360*DPI_MUL, DRED);
        fb_msgbox_add_text(-1, 30*DPI_MUL, SIZE_BIG, "Error");
        fb_msgbox_add_text(-1, 90*DPI_MUL, SIZE_NORMAL, "Unsupported ROM type.");
        fb_msgbox_add_text(-1, 180*DPI_MUL, SIZE_NORMAL, "Please see XDA thread.");
        fb_msgbox_add_text(-1, active_msgbox->h-60*DPI_MUL, SIZE_NORMAL, "Touch to close");

        fb_draw();
        fb_freeze(1);
        set_touch_handlers_mode(HANDLERS_ALL);
        return;
    }
#endif
    if(rom->type == ROM_TYPE_ANDROID_IMG && rom->android_img->kernel_path != NULL &&
        multirom_has_kexec() != 0)
    {
        active_msgbox = fb_create_msgbox(416*DPI_MUL, 360*DPI_MUL, DRED);
        fb_msgbox_add_text(-1, 30*DPI_MUL, SIZE_BIG, "Error");
        fb_msgbox_add_text(-1, 90*DPI_MUL, SIZE_NORMAL, "Kexec-hardboot support is");
        fb_msgbox_add_text(-1, 125*DPI_MUL, SIZE_NORMAL, "required to boot this ROM.");
        fb_msgbox_add_text(-1, 180*DPI_MUL, SIZE_NORMAL, "Please Use kernel with");
        fb_msgbox_add_text(-1, 215*DPI_MUL, SIZE_NORMAL, "kexec-hardboot support.");
        fb_msgbox_add_text(-1, active_msgbox->h-60*DPI_MUL, SIZE_NORMAL, "Touch to close");

        fb_draw();
        fb_freeze(1);
        set_touch_handlers_mode(HANDLERS_ALL);
        return;
    }
#if 0
    // Partitions are unmounted when scanned, so...
    // TODO: check the partition more strictly
    if(rom->partition->type != PART_INTERNAL && !multirom_mount_partition(rom->partition))
    {
        active_msgbox = fb_create_msgbox(416*DPI_MUL, 360*DPI_MUL, DRED);
        fb_msgbox_add_text(-1, 30*DPI_MUL, SIZE_BIG, "Error");
        switch(rom->partition->type)
        {
        case PART_EXTERNAL_SD:
            fb_msgbox_add_text(-1, 90*DPI_MUL, SIZE_NORMAL, "The SD card partition");
            break;
        case PART_EXTERNAL_USBDISK:
            fb_msgbox_add_text(-1, 90*DPI_MUL, SIZE_NORMAL, "The USB storage partition");
            break;
        default:
            fb_msgbox_add_text(-1, 90*DPI_MUL, SIZE_NORMAL, "The partition");
            break;
        }
        fb_msgbox_add_text(-1, 125*DPI_MUL, SIZE_NORMAL, "cannot be mounted!");
        fb_msgbox_add_text(-1, 180*DPI_MUL, SIZE_NORMAL, "The ROM list will be");
        fb_msgbox_add_text(-1, 215*DPI_MUL, SIZE_NORMAL, "refreshed automatically.");
        fb_msgbox_add_text(-1, active_msgbox->h-60*DPI_MUL, SIZE_NORMAL, "Touch to close");

        fb_draw();
        fb_freeze(1);
        set_touch_handlers_mode(HANDLERS_ALL);
        return;
    }
//#if 0
    if((m & MASK_KEXEC) && strchr(rom->name, ' '))
    {
        active_msgbox = fb_create_msgbox(416*DPI_MUL, 360*DPI_MUL, DRED);
        fb_msgbox_add_text(-1, 30*DPI_MUL, SIZE_BIG, "Error");
        fb_msgbox_add_text(-1, 90*DPI_MUL, SIZE_NORMAL, "ROM name contains spaces");
        fb_msgbox_add_text(-1, 180*DPI_MUL, SIZE_NORMAL, "Please remove spaces");
        fb_msgbox_add_text(-1, active_msgbox->h-60*DPI_MUL, SIZE_NORMAL, "Touch to close");

        fb_draw();
        fb_freeze(1);
        set_touch_handlers_mode(HANDLERS_ALL);
        return;
    }
#endif

    pthread_mutex_lock(&exit_code_mutex);
    selected_rom = rom;
    selected_profile = profile;
    exit_ui_code = UI_EXIT_BOOT_ROM;
    pthread_mutex_unlock(&exit_code_mutex);
}

// TODO: usb hot plugging support
#if 0
void multirom_ui_tab_rom_update_usb(void *data)
{
    tab_data_roms *t = (tab_data_roms*)themes_info->data->tab_data;
    listview_clear(t->list);

    t->rom_name->text = realloc(t->rom_name->text, 1);
    t->rom_name->text[0] = 0;

    multirom_ui_fill_rom_list(t->list, MASK_USB_ROMS);
    listview_update_ui(t->list);

    multirom_ui_tab_rom_set_empty(data, (int)(t->list->items == NULL));
    fb_draw();
}

void multirom_ui_tab_rom_refresh_usb(int action)
{
    multirom_update_partitions(mrom_status);
}
#endif

void multirom_ui_tab_rom_set_empty(tab_data_roms *data, int empty, int tab_type)
{
    assert(empty == 0 || empty == 1);

    tab_data_roms *t = data;
    int width = cur_theme->get_tab_width(themes_info->data);

    static const char *str[] = { "Select boot ROM:", "No ROMs are found!" };
    t->title_text->head.x = center_x(t->list->x, width, SIZE_BIG, str[empty]);
    t->title_text->text = realloc(t->title_text->text, strlen(str[empty])+1);
    strcpy(t->title_text->text, str[empty]);

    if(t->boot_btn)
        button_enable(t->boot_btn, !empty);

    // TODO: usb hot plugging
    if(0 && empty && tab_type == TAB_USB && !t->usb_text)
    {
        const int line_len = 29;
        static const char *txt = "Please plug in the USB drive.\nThis list will update itself.";
        int x = t->list->x + (width/2 - (line_len*ISO_CHAR_WIDTH*SIZE_NORMAL)/2);
        int y = center_y(t->list->y, t->list->h, SIZE_NORMAL);
        t->usb_text = fb_add_text(x, y, WHITE, SIZE_NORMAL, txt);
        list_add(t->usb_text, &t->ui_elements);

        x = t->list->x + ((width/2) - (PROGDOTS_W/2));
        t->usb_prog = progdots_create(x, y+100*DPI_MUL);
    }
    else if(!empty && t->usb_text)
    {
        progdots_destroy(t->usb_prog);
        t->usb_prog = NULL;

        list_rm(t->usb_text, &t->ui_elements, &fb_remove_item);
        t->usb_text = NULL;
    }
}

void *multirom_ui_tab_misc_init(void)
{
    tab_data_misc *t = mzalloc(sizeof(tab_data_misc));
    cur_theme->tab_misc_init(themes_info->data, t, multirom_status.pref.color);
    return t;
}

void multirom_ui_tab_misc_destroy(void *data)
{
    tab_data_misc *t = (tab_data_misc*)data;

    list_clear(&t->ui_elements, &fb_remove_item);
    list_clear(&t->buttons, &button_destroy);

    free(t);
}

void multirom_ui_tab_misc_change_clr(int clr)
{
    if((loop_act & LOOP_CHANGE_CLR) || multirom_status.pref.color == clr)
        return;

    pthread_mutex_lock(&exit_code_mutex);
    multirom_status.pref.color = clr;
    loop_act |= LOOP_CHANGE_CLR;
    pthread_mutex_unlock(&exit_code_mutex);
}

void multirom_ui_reboot_btn(int action)
{
    pthread_mutex_lock(&exit_code_mutex);
    exit_ui_code = action;
    pthread_mutex_unlock(&exit_code_mutex);
}

void multirom_ui_tab_misc_copy_log(int action)
{
    //multirom_dump_status(mrom_status);

    int res = multirom_copy_log(NULL);

    static const char *text[] = { "Failed!", "Success!" };

    active_msgbox = fb_create_msgbox(416*DPI_MUL, 260*DPI_MUL, res ? DRED : CLR_PRIMARY);
    fb_msgbox_add_text(-1, 50*DPI_MUL, SIZE_NORMAL, (char*)text[res+1]);
    if(res == 0)
        fb_msgbox_add_text(-1, -1, SIZE_NORMAL, "sdcard/multirom_error.txt");
    fb_msgbox_add_text(-1, active_msgbox->h-60*DPI_MUL, SIZE_NORMAL, "Touch to close");

    fb_draw();
    fb_freeze(1);
    set_touch_handlers_mode(HANDLERS_ALL);
}

void multirom_ui_tab_misc_rescan(int action)
{
    pthread_mutex_lock(&exit_code_mutex);
    loop_act |= LOOP_EXT_RESCAN;
    pthread_mutex_unlock(&exit_code_mutex);
}
