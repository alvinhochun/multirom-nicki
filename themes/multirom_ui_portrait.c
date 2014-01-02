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

#include "../multirom_ui.h"
#include "../multirom_ui_themes.h"
#include "../framebuffer.h"
#include "../util.h"
#include "../button.h"
#include "../version.h"
#include "../input.h"

#define PADDING_L (16 * DPI_MUL)
#define PADDING_M (8 * DPI_MUL)
#define PADDING_S (4 * DPI_MUL)

#define HEADER_TEXT_H (ISO_CHAR_HEIGHT * SIZE_NORMAL + PADDING_S * 2)
#define HEADER_TABS_H (ISO_CHAR_HEIGHT * SIZE_NORMAL + PADDING_M * 2 + 6 * DPI_MUL)
#define HEADER_HEIGHT (HEADER_TEXT_H + HEADER_TABS_H)

#define ROMS_HEADER_H (ISO_CHAR_HEIGHT * SIZE_BIG + PADDING_L * 2)
#define BOOTBTN_W (180 * DPI_MUL)
#define BOOTBTN_H (80 * DPI_MUL)
#define ROMS_FOOTER_H (BOOTBTN_H + PADDING_L * 2)

#define MISCBTN_W (fb_width - PADDING_L * 2)
#define MISCBTN_H (60 * DPI_MUL)

#define CLRBTN_W (48 * DPI_MUL)
#define CLRBTN_B (PADDING_M * 2)
#define CLRBTN_TOTAL (CLRBTN_W + CLRBTN_B)
#define CLRBTN_Y (fb_height - CLRBTN_TOTAL * 2)

static button *pong_btn = NULL;
static void destroy(multirom_theme_data *t)
{
    button_destroy(pong_btn);
    pong_btn = NULL;
}

static void init_header(multirom_theme_data *t)
{
    button **tab_btns = t->tab_btns;
    fb_text **tab_texts = t->tab_texts;

    int i, tab_x, tab_width;

    static const char *header = "MultiROM for Xperia M/M Dual";
    static const char *tabs[TAB_COUNT] = { "Internal", "microSD", "USB", "Misc" };

    fb_add_text(PADDING_S, PADDING_S, WHITE, SIZE_NORMAL, header);

    pong_btn = mzalloc(sizeof(button));
    // No matter how long the header is, only "MultiROM" is clickable
    pong_btn->w = strlen("MultiROM") * ISO_CHAR_WIDTH * SIZE_NORMAL + PADDING_S * 2;
    pong_btn->h = HEADER_TEXT_H;
    pong_btn->clicked = &multirom_ui_start_pong;
    button_init_ui(pong_btn, NULL, 0);

    tab_x = fb_width;
    for(i = TAB_COUNT - 1; i >= 0; --i)
    {
        tab_width = strlen(tabs[i]) * ISO_CHAR_WIDTH * SIZE_NORMAL + PADDING_M * 2 + 2 * DPI_MUL;
        tab_x -= tab_width;
        tab_texts[i] = fb_add_text(tab_x + PADDING_M + 2*DPI_MUL, HEADER_TEXT_H + PADDING_M + 6 * DPI_MUL, WHITE, SIZE_NORMAL, tabs[i]);

        // Vertical tab separator
        fb_add_rect(tab_x, HEADER_TEXT_H + 6 * DPI_MUL, 2, HEADER_TABS_H - 6, GRAY);
        // Top border
        fb_add_rect(tab_x + 2, HEADER_TEXT_H + 4 * DPI_MUL, tab_width - 4 * DPI_MUL, 2, GRAY);

        tab_btns[i] = malloc(sizeof(button));
        memset(tab_btns[i], 0, sizeof(button));
        tab_btns[i]->x = tab_x;
        tab_btns[i]->y = HEADER_TEXT_H;
        tab_btns[i]->w = tab_width;
        tab_btns[i]->h = HEADER_TABS_H;
        tab_btns[i]->action = i;
        tab_btns[i]->clicked = &multirom_ui_switch;
        button_init_ui(tab_btns[i], NULL, 0);
    }

    for(i = 0; i < TAB_COUNT; ++i)
    {
        keyaction_add(tab_btns[i]->x, tab_btns[i]->y, button_keyaction_call, tab_btns[i]);
    }

    fb_add_rect(0, HEADER_HEIGHT - 2, fb_width, 2, WHITE);
}

static void header_select(multirom_theme_data *t, int tab)
{
    int i;
    for(i = 0; i < TAB_COUNT; ++i)
    {
        t->tab_texts[i]->head.y = HEADER_TEXT_H + PADDING_M + ((i == tab) ? 4 * DPI_MUL : 6 * DPI_MUL);
        t->tab_texts[i]->color = (i == tab) ? BLACK : WHITE;
    }

    if(!t->selected_tab_rect)
        t->selected_tab_rect = fb_add_rect(0, HEADER_TEXT_H, 0, HEADER_TABS_H, WHITE);

    t->selected_tab_rect->head.x = t->tab_btns[tab]->x;
    t->selected_tab_rect->w = t->tab_btns[tab]->w;
}

static void tab_rom_init(multirom_theme_data *t, tab_data_roms *d, int tab_type)
{
    int footer_y = fb_height - ROMS_FOOTER_H;

    d->rom_name = fb_add_text(0, footer_y + ROMS_FOOTER_H / 2 - ISO_CHAR_HEIGHT * SIZE_NORMAL,
                              WHITE, SIZE_NORMAL, "");
    d->rom_profile = fb_add_text(0, footer_y + ROMS_FOOTER_H / 2,
                              WHITE, SIZE_NORMAL, "");

    d->list->y = HEADER_HEIGHT + ROMS_HEADER_H;
    d->list->w = fb_width;
    d->list->h = fb_height - d->list->y - ROMS_FOOTER_H - 2;

    // header
    int y = center_y(HEADER_HEIGHT, ROMS_HEADER_H, SIZE_BIG);
    d->title_text = fb_add_text(0, y, CLR_PRIMARY, SIZE_BIG, "");
    list_add(d->title_text, &d->ui_elements);

    // footer
    fb_rect *sep = fb_add_rect(0, footer_y - 2, fb_width, 2, CLR_PRIMARY);
    list_add(sep, &d->ui_elements);

    // boot btn
    d->boot_btn->x = fb_width - BOOTBTN_W - PADDING_L;
    d->boot_btn->y = footer_y + PADDING_L;
    d->boot_btn->w = BOOTBTN_W;
    d->boot_btn->h = BOOTBTN_H;

    keyaction_add(d->boot_btn->x, d->boot_btn->y, button_keyaction_call, d->boot_btn);
}

static void tab_misc_init(multirom_theme_data *t, tab_data_misc *d, int color_scheme)
{
    int x = fb_width / 2 - MISCBTN_W / 2;
    int y = HEADER_HEIGHT + PADDING_L * 4;

    button *b = mzalloc(sizeof(button));
    b->x = x;
    b->y = y;
    b->w = MISCBTN_W;
    b->h = MISCBTN_H;
    b->clicked = &multirom_ui_tab_misc_copy_log;
    button_init_ui(b, "Copy log to /sdcard", SIZE_NORMAL);
    list_add(b, &d->buttons);

    y += MISCBTN_H + PADDING_L * 3;

    static const struct {
        const int exit_code;
        const char *text;
    } cmds[] = {
        { UI_EXIT_REBOOT, "Reboot" },
        { UI_EXIT_REBOOT_RECOVERY, "Recovery" },
        { UI_EXIT_REBOOT_BOOTLOADER, "Bootloader" },
        { UI_EXIT_POWEROFF, "Power Off" },
        { 0, NULL },
    };

    int i;
    for(i = 0; cmds[i].text; ++i)
    {
        b = mzalloc(sizeof(button));
        b->x = x;
        b->y = y;
        b->w = MISCBTN_W;
        b->h = MISCBTN_H;
        b->action = cmds[i].exit_code;
        b->clicked = &multirom_ui_reboot_btn;
        button_init_ui(b, cmds[i].text, SIZE_BIG);
        list_add(b, &d->buttons);

        y += MISCBTN_H + PADDING_L;
    }

    fb_text *text = fb_add_text(0, fb_height - ISO_CHAR_HEIGHT * SIZE_SMALL, WHITE, SIZE_SMALL, "MultiROM v%d"VERSION_DEV_FIX" with trampoline v%d.",
                               VERSION_MULTIROM, multirom_get_trampoline_ver());
    list_add(text, &d->ui_elements);

#ifdef MR_UNOFFICIAL_TEXT
    text = fb_add_text(0, fb_height - ISO_CHAR_HEIGHT * SIZE_SMALL * 2, WHITE, SIZE_SMALL, "Unofficial "MR_UNOFFICIAL_TEXT" "__DATE__" "__TIME__);
    list_add(text, &d->ui_elements);
#endif

    char bat_text[16];
    sprintf(bat_text, "Battery: %d%%", multirom_get_battery());
    text = fb_add_text_long(fb_width - strlen(bat_text) * ISO_CHAR_WIDTH * SIZE_SMALL, fb_height - ISO_CHAR_HEIGHT * SIZE_SMALL, WHITE, SIZE_SMALL, bat_text);
    list_add(text, &d->ui_elements);

    x = fb_width / 2 - (CLRS_MAX * CLRBTN_TOTAL) / 2;
    uint32_t p, s;
    fb_rect *r;
    for(i = 0; i < CLRS_MAX; ++i)
    {
        multirom_ui_setup_colors(i, &p, &s);

        if(i == color_scheme)
        {
            r = fb_add_rect(x, CLRBTN_Y, CLRBTN_TOTAL, CLRBTN_TOTAL, WHITE);
            list_add(r, &d->ui_elements);
        }

        r = fb_add_rect(x + CLRBTN_B / 2, CLRBTN_Y + CLRBTN_B / 2, CLRBTN_W, CLRBTN_W, p);
        list_add(r, &d->ui_elements);

        b = mzalloc(sizeof(button));
        b->x = x;
        b->y = CLRBTN_Y;
        b->w = CLRBTN_TOTAL;
        b->h = CLRBTN_TOTAL;
        b->action = i;
        b->clicked = &multirom_ui_tab_misc_change_clr;
        button_init_ui(b, NULL, 0);
        list_add(b, &d->buttons);

        x += CLRBTN_TOTAL;
    }

    for(i = 0; d->buttons[i]; ++i)
        keyaction_add(d->buttons[i]->x, d->buttons[i]->y, button_keyaction_call, d->buttons[i]);
}

static int get_tab_width(multirom_theme_data *t)
{
    return fb_width;
}

static int get_tab_height(multirom_theme_data *t)
{
    return fb_height - HEADER_HEIGHT;
}

static void center_rom_name(tab_data_roms *d, const char *name, const char *profile)
{
    d->rom_name->head.x = center_x(0, fb_width - BOOTBTN_W - 20, SIZE_NORMAL, name);
    if(d->rom_name->head.x < 0)
        d->rom_name->head.x = 0;

    d->rom_profile->head.x = center_x(0, fb_width - BOOTBTN_W - 20, SIZE_NORMAL, profile);
    if(d->rom_profile->head.x < 0)
        d->rom_profile->head.x = 0;
}

const struct multirom_theme theme_info_portrait = {
    .width = TH_PORTRAIT,
    .height = TH_PORTRAIT,

    .destroy = &destroy,
    .init_header = &init_header,
    .header_select = &header_select,
    .tab_rom_init = &tab_rom_init,
    .tab_misc_init = &tab_misc_init,
    .get_tab_width = &get_tab_width,
    .get_tab_height = &get_tab_height,
    .center_rom_name = &center_rom_name
};
