/* Copyright (c) 2021-2024
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public
* License as published by the Free Software Foundation; either
* version 2 of the License, or (at your option) any later version.
#
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* General Public License for more details.
#
* You should have received a copy of the GNU General Public
* License along with this program; if not, write to the
* Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
* Boston, MA 02110-1301 USA
#
* Authored by: Kris Henriksen <krishenriksen.work@gmail.com>
#
* AnberPorts-Keyboard-Mouse
* 
* Part of the code is from from https://github.com/krishenriksen/AnberPorts/blob/master/AnberPorts-Keyboard-Mouse/main.c (mostly the fake keyboard)
* Fake Xbox code from: https://github.com/Emanem/js2xbox
* 
* Modified (badly) by: Shanti Gilbert for EmuELEC
* Modified further by: Nikolai Wuttke for EmuELEC (Added support for SDL and the SDLGameControllerdb.txt)
* Modified further by: Jacob Smith
* 
* Any help improving this code would be greatly appreciated! 
* 
* DONE: Xbox360 mode: Fix triggers so that they report from 0 to 255 like real Xbox triggers
*       Xbox360 mode: Figure out why the axis are not correctly labeled?  SDL_CONTROLLER_AXIS_RIGHTX / SDL_CONTROLLER_AXIS_RIGHTY / SDL_CONTROLLER_AXIS_TRIGGERLEFT / SDL_CONTROLLER_AXIS_TRIGGERRIGHT
*       Keyboard mode: Add a config file option to load mappings from.
*       add L2/R2 triggers
* 
*/

#include "gptokeyb2.h"
#include "ini.h"

#define MAX_TEMP_SIZE 1024


enum
{
    CFG_GPTK,
    CFG_CONFIG,
    CFG_CONTROL,
    CFG_OTHER,
};


typedef struct
{
    int state;
    bool config_only;
    char last_section[MAX_CONTROL_NAME];
    gptokeyb_config *current_config;
} config_parser;


gptokeyb_config *root_config = NULL;
gptokeyb_config *config_stack[CFG_STACK_MAX];
int gptokeyb_config_depth = 0;


const char *gbtn_names[] = {
    "a",
    "b",
    "x",
    "y",

    "l1",
    "l2",
    "l3",

    "r1",
    "r2",
    "r3",

    "start",
    "back",
    "guide",

    "up",
    "down",
    "left",
    "right",

    "left_analog_up",
    "left_analog_down",
    "left_analog_left",
    "left_analog_right",

    "right_analog_up",
    "right_analog_down",
    "right_analog_left",
    "right_analog_right",

    // SPECIAL
    "(max)",

    // SPECIAL
    "dpad",
    "left_analog",
    "right_analog",
};


const char *act_names[] = {
    "(none)",
    "parent",
    "mouse_slow",
    "mouse_move",
    "hold_state",
    "state_push",
    "state_set",
    "state_pop",
};


int special_button_min(int btn)
{
    if (btn == GBTN_DPAD)
        return GBTN_DPAD_UP;

    if (btn == GBTN_LEFT_ANALOG)
        return GBTN_LEFT_ANALOG_UP;

    if (btn == GBTN_RIGHT_ANALOG)
        return GBTN_RIGHT_ANALOG_UP;

    return 0;
}


int special_button_max(int btn)
{
    if (btn == GBTN_DPAD)
        return GBTN_DPAD_RIGHT+1;

    if (btn == GBTN_LEFT_ANALOG)
        return GBTN_LEFT_ANALOG_RIGHT+1;

    if (btn == GBTN_RIGHT_ANALOG)
        return GBTN_RIGHT_ANALOG_RIGHT+1;

    return 0;
}


void config_init()
{   // Setup config structures.
    root_config = (gptokeyb_config*)malloc(sizeof(gptokeyb_config));
    if (root_config == NULL)
    {
        fprintf(stderr, "Unable to allocate memory. :(");
        exit(255);
    }

    memset((void*)root_config, '\0', sizeof(gptokeyb_config));
    strcpy(root_config->name, "controls");
    gptokeyb_config_depth = 0;

    config_stack[0] = root_config;

    for (int i=1; i < CFG_STACK_MAX; i++)
    {
        config_stack[i] = NULL;
    }
}


void config_quit()
{   // Destroy config structures.
    gptokeyb_config *current = root_config;
    gptokeyb_config *next;

    while (current != NULL)
    {
        next = current->next;
        free(current);
        current = next;
    }

    for (int i=0; i < CFG_STACK_MAX; i++)
    {
        config_stack[i] = NULL;
    }
}


void config_dump()
{   // Dump all the current configs.
    gptokeyb_config *current = root_config;

    while (current != NULL)
    {
        printf("-------------------------------------------\n");
        printf("- %s\n", current->name);
        printf("\n");
        for (int btn=0; btn < GBTN_MAX; btn++)
        {
            printf("%s =", gbtn_names[btn]);

            if (current->button[btn].keycode != 0)
            {
                printf(" \"%s\"", find_keycode(current->button[btn].keycode));

                if ((current->button[btn].modifier & MOD_ALT) != 0)
                    printf(" mod_alt");

                if ((current->button[btn].modifier & MOD_SHIFT) != 0)
                    printf(" mod_shift");

                if ((current->button[btn].modifier & MOD_CTRL) != 0)
                    printf(" mod_ctrl");
            }

            if (current->button[btn].action != 0)
                printf(" %s %s", act_names[current->button[btn].action], current->button[btn].cfg_name);

            printf("\n");
        }

        current = current->next;
        printf("\n");
    }

    printf("-------------------------------------------\n");
}


void config_overlay_parent(gptokeyb_config *current)
{
    for (int btn=0; btn < GBTN_MAX; btn++)
    {
        current->button[btn].keycode = 0;
        current->button[btn].modifier = 0;
        current->button[btn].action = ACT_PARENT;
    }
}


void config_overlay_named(gptokeyb_config *current, const char *name)
{
    gptokeyb_config *other = config_find(name);
    if (other == NULL)
    {
        fprintf(stderr, "overlay %s: unable to find config\n", name);
        return;
    }

    if (current == other)
    {
        fprintf(stderr, "overlay %s: unable to overlay to the same config\n", name);
        return;
    }

    fprintf(stderr, "overlay %s: \n", other->name);

    for (int btn=0; btn < GBTN_MAX; btn++)
    {
        current->button[btn].keycode = other->button[btn].keycode;
        current->button[btn].modifier = other->button[btn].modifier;
        current->button[btn].action = other->button[btn].action;

        if (current->button[btn].action >= ACT_STATE_HOLD)
        {
            strncpy(current->button[btn].cfg_name, other->button[btn].cfg_name, MAX_CONTROL_NAME-1);
            current->map_check = true;
        }
    }

}


gptokeyb_config *config_find(const char *name)
{   // Find a gptokeyb_config by name.
    char nice_name[MAX_CONTROL_NAME];

    // shortcut
    if (strcasecmp("controls", name) == 0)
    {
        // GPTK2_DEBUG("config_find: found %s\n", name);
        return root_config;
    }

    if (!strcasestartswith(name, "controls:"))
    {
        snprintf(nice_name, MAX_CONTROL_NAME-1, "controls:%s", name);
        name=nice_name;
    }

    gptokeyb_config *current = root_config;

    while (current != NULL)
    {
        if (strcasecmp(current->name, name) == 0)
        {
            // GPTK2_DEBUG("config_find: found %s\n", name);
            return current;
        }

        current = current->next;
    }

    // GPTK2_DEBUG("config_find: unable to find %s\n", name);

    return NULL;
}


gptokeyb_config *config_create(const char *name)
{   // find a config, if it doesnt exist, create it.
    gptokeyb_config *result=NULL;

    result = config_find(name);
    if (result != NULL)
        return result;

    result = (gptokeyb_config*)malloc(sizeof(gptokeyb_config));
    if (result == NULL)
    {
        fprintf(stderr, "Unable to allocate memory. :(");
        exit(255);
    }

    memset((void*)result, '\0', sizeof(gptokeyb_config));

    if (!strcasestartswith(name, "controls:"))
    {
        snprintf(result->name, MAX_CONTROL_NAME-1, "controls:%s", name);
    }
    else
    {
        strncpy(result->name, name, MAX_CONTROL_NAME-1);
    }

    result->next = root_config->next;
    root_config->next = result;

    // GPTK2_DEBUG("config_create: %s\n", result->name);
    return result;
}

void set_cfg_config(const char *name, const char *value)
{
    return;
}

void set_btn_config(gptokeyb_config *config, int btn, const char *name, const char *value)
{   // this parses a keybinding
    /*
     * Keybindings can be in the form:
     * 
     *   a = f1
     *   a = add_alt
     */

    char temp_buffer[MAX_TEMP_SIZE];
    int value_len = strlen(value);
    char *token;

    if (value_len == 0)
        return;

    if (value_len > (MAX_TEMP_SIZE-1))
        value_len = MAX_TEMP_SIZE-1;

    int t=0;
    int i=0;

    // we parse the config line, we want to break it up into tab separated items.
    while (i < value_len)
    {
        if (value[i] == '"' || value[i] == '\'')
        {
            char c = value[i];
            i += 1;

            while (value[i] != c && i < value_len)
                temp_buffer[t++] = value[i++];

            temp_buffer[t++] = '\t';
            i++;
        }
        else if (value[i] == ' ' || value[i] == '\t')
        {
            if (t > 0 && temp_buffer[t-1] != '\t')
                temp_buffer[t++] = '\t';
            i++;

            while ((value[i] == ' ' || value[i] == '\t') && i < value_len)
                i++;
        }
        else
        {
            temp_buffer[t++] = value[i++];

            while (value[i] != ' ' && value[i] != '\t' && i < value_len)
                temp_buffer[t++] = value[i++];
        }
    }
    temp_buffer[t] = '\0';

    // GPTK2_DEBUG(">>> '%s'\n", value);
    // GPTK2_DEBUG("    '%s'\n", temp_buffer);
    bool first_run = true;
    token = strtok(temp_buffer, "\t");

    while (token != NULL)
    {
        if (strcasecmp(token, "mouse_slow") == 0)
        {
            // Can't set mouse_slow to the special buttons
            if (btn >= GBTN_MAX)
            {
                fprintf(stderr, "error: unable to set %s to %s\n", token, gbtn_names[btn]);
                return;
            }

            config->button[btn].action = ACT_MOUSE_SLOW;
        }
        else if (strcasecmp(token, "hold_state") == 0)
        {
            if (btn >= GBTN_MAX)
            {
                fprintf(stderr, "error: unable to set %s to %s\n", token, gbtn_names[btn]);
                return;
            }

            token = strtok(NULL, "\t");

            if (token == NULL)
                return;

            config->button[btn].action = ACT_STATE_HOLD;
            strncpy(config->button[btn].cfg_name, token, MAX_CONTROL_NAME);
            config->map_check = true;
        }
        else if (strcasecmp(token, "push_state") == 0)
        {
            if (btn >= GBTN_MAX)
            {
                fprintf(stderr, "error: unable to set %s to %s\n", token, gbtn_names[btn]);
                return;
            }

            token = strtok(NULL, "\t");

            if (token == NULL)
                return;

            config->button[btn].action = ACT_STATE_PUSH;
            strncpy(config->button[btn].cfg_name, token, MAX_CONTROL_NAME);
            config->map_check = true;
        }
        else if (strcasecmp(token, "set_state") == 0)
        {
            token = strtok(NULL, "\t");
            if (token == NULL)
                continue;

            if (btn >= GBTN_MAX)
            {
                fprintf(stderr, "error: unable to set %s to %s\n", token, gbtn_names[btn]);
                return;
            }

            config->button[btn].action = ACT_STATE_PUSH;
            strncpy(config->button[btn].cfg_name, token, MAX_CONTROL_NAME);
            config->map_check = true;
        }
        else if (strcasecmp(token, "pop_state") == 0)
        {
            if (btn >= GBTN_MAX)
            {
                fprintf(stderr, "error: unable to set %s to %s\n", token, gbtn_names[btn]);
                return;
            }

            config->button[btn].action = ACT_STATE_POP;
        }
        else if ((strcasecmp(token, "add_alt") == 0) || (!first_run && (strcasecmp(token, "alt") == 0)))
        {
            if (btn >= GBTN_MAX)
            {
                for (int sbtn=special_button_min(btn); sbtn < special_button_max(btn); sbtn++)
                    config->button[sbtn].modifier |= MOD_ALT;
            }
            else
            {
                config->button[btn].modifier |= MOD_ALT;
            }
        }
        else if ((strcasecmp(token, "add_ctrl") == 0) || (!first_run && (strcasecmp(token, "ctrl") == 0)))
        {
            if (btn >= GBTN_MAX)
            {
                for (int sbtn=special_button_min(btn); sbtn < special_button_max(btn); sbtn++)
                    config->button[sbtn].modifier |= MOD_CTRL;
            }
            else
            {
                config->button[btn].modifier |= MOD_CTRL;
            }
        }
        else if ((strcasecmp(token, "add_shift") == 0) || (!first_run && (strcasecmp(token, "shift") == 0)))
        {
            if (btn >= GBTN_MAX)
            {
                for (int sbtn=special_button_min(btn); sbtn < special_button_max(btn); sbtn++)
                    config->button[sbtn].modifier |= MOD_SHIFT;
            }
            else
            {
                config->button[btn].modifier |= MOD_SHIFT;
            }
        }
        else if (strcasecmp(token, "repeat") == 0)
        {
            if (btn >= GBTN_MAX)
            {
                for (int sbtn=special_button_min(btn); sbtn < special_button_max(btn); sbtn++)
                    config->button[sbtn].repeat = true;
            }
            else
            {
                config->button[btn].repeat = true;
            }
        }
        else
        {
            const keyboard_values *key = find_keyboard(token);

            if (key != NULL)
            {
                if (btn >= GBTN_MAX)
                {
                    for (int sbtn=special_button_min(btn); sbtn < special_button_max(btn); sbtn++)
                    {
                        config->button[sbtn].keycode = key->keycode;
                        config->button[sbtn].action = ACT_NONE;
                    }
                }
                else
                {
                    config->button[btn].keycode = key->keycode;                    
                    config->button[btn].action = ACT_NONE;
                    // CEBION SAID NO
                    // config->button[btn].modifier |= key->modifier;
                }
            }
            else if (strcasecmp(token, "mouse_movement") == 0)
            {
                if (btn >= GBTN_MAX)
                {
                    for (int sbtn=special_button_min(btn), i=0; sbtn < special_button_max(btn); sbtn++, i++)
                    {
                        config->button[sbtn].keycode = 0;
                        config->button[sbtn].action = ACT_MOUSE_MOVE;
                    }
                }
                else
                {
                    fprintf(stderr, "error: unable to set %s to %s\n", token, gbtn_names[btn]);
                    return;
                }
            }
            else if (strcasecmp(token, "arrow_keys") == 0)
            {
                if (btn >= GBTN_MAX)
                {
                    short keycodes[] = {KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT};
                    for (int sbtn=special_button_min(btn), i=0; sbtn < special_button_max(btn); sbtn++, i++)
                    {
                        config->button[sbtn].keycode = keycodes[i];
                        config->button[sbtn].action = ACT_NONE;
                    }
                }
                else
                {
                    fprintf(stderr, "error: unable to set %s to %s\n", token, gbtn_names[btn]);
                    return;
                }
            }
            else
            {
                GPTK2_DEBUG("unknown key %s\n", token);
            }
        }

        token = strtok(NULL, "\t");
        first_run = false;
    }
}


static int config_ini_handler(
    void* user, const char* section, const char* name, const char* value)
{
    config_parser* config = (config_parser*)user;

    if (strcmp(config->last_section, section) != 0)
    {
        GPTK2_DEBUG("%s:\n", section);
        strncpy(config->last_section, section, MAX_CONTROL_NAME-1);

        if (strcasecmp(section, "config") == 0)
        {
            GPTK2_DEBUG("CONFIG\n");
            config->state = CFG_CONFIG;
        }
        else if (strcasecmp(section, "controls") == 0)
        {
            GPTK2_DEBUG("CONTROLS\n");
            config->state = CFG_CONTROL;
            config->current_config = root_config;
        }
        else if (strcasestartswith(section, "controls:"))
        {
            GPTK2_DEBUG("CONTROLS++\n");
            config->state = CFG_CONTROL;
            config->current_config = config_create(section);
        }
        else
        {
            GPTK2_DEBUG("OTHER\n");
            config->state = CFG_OTHER;
        }
    }

    if (config->state == CFG_GPTK)
    {   // GPTK mode. :(
        const button_match *button = find_button(name);

        if (button != NULL)
        {
            set_btn_config(config->current_config, button->gbtn, name, value);
            GPTK2_DEBUG("G: %s: %s, (%s, %d)\n", name, value, button->str, button->gbtn);
        }
        else if (strcasecmp(name, "overlay") == 0)
        {
            if (strcasecmp(value, "parent") == 0)
            {
                GPTK2_DEBUG("overlay = parent\n");
                config_overlay_parent(config->current_config);
            }
            else if (strcasecmp(value, "clear") == 0)
            {
                GPTK2_DEBUG("overlay = clear\n");
                // DO NOTHING
            }
            else if (strlen(value) > 0)
            {
                GPTK2_DEBUG("overlay = %s\n", value);
                config_overlay_named(config->current_config, value);
            }
            else
            {
                fprintf(stderr, "overlay = (blank)\n");
            }
        }
        else
        {
            set_cfg_config(name, value);
            GPTK2_DEBUG("G: %s: %s\n", name, value);
        }
    }

    else if (config->state == CFG_CONFIG)
    {   // config mode.
        set_cfg_config(name, value);
        GPTK2_DEBUG("C: %s: %s\n", name, value);
    }

    else if (config->state == CFG_CONTROL)
    {   // controls mode.
        const button_match *button = find_button(name);

        if (button != NULL)
        {
            set_btn_config(config->current_config, button->gbtn, name, value);
            GPTK2_DEBUG("X: %s: %s (%s, %d)\n", name, value, button->str, button->gbtn);
        }
        else if (strcasecmp(name, "overlay") == 0)
        {
            if (strcasecmp(value, "parent") == 0)
            {
                GPTK2_DEBUG("overlay = parent\n");
                config_overlay_parent(config->current_config);
            }
            else if (strcasecmp(value, "clear") == 0)
            {
                GPTK2_DEBUG("overlay = clear\n");
                // DO NOTHING
            }
            else if (strlen(value) > 0)
            {
                GPTK2_DEBUG("overlay = %s\n", value);
                config_overlay_named(config->current_config, value);
            }
            else
            {
                fprintf(stderr, "overlay = (blank)\n");
            }
        }
        else
        {
            GPTK2_DEBUG("X: %s: %s\n", name, value);
        }
    }

    else
    {
        GPTK2_DEBUG("?: %s: %s\n", name, value);
    }

    return 1;
}


int config_load(const char *file_name, bool config_only)
{
    config_parser config;

    memset((void*)&config, '\0', sizeof(config_parser));

    config.state = CFG_GPTK;
    config.current_config = root_config;
    config.config_only = config_only;

    if (config_only)
        config.state = CFG_CONFIG;

    if (ini_parse(file_name, config_ini_handler, &config) < 0)
    {
        printf("Can't load '%s'\n", file_name);
        return 1;
    }

    return 0;
}