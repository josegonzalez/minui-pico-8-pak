/*
 * The in-game menu, laid out as minarch's: Continue, Save, Load, Options and
 * Quit over the dimmed game, with a slot for Save and Load chosen with Left
 * and Right and a preview of what it holds, and an Options page of groups,
 * Frontend, Emulator, Splore and Shortcuts, for the pak's settings and the
 * PICO-8 settings it writes into config.txt. Save and Load are shown but
 * unavailable where there are no save states, as in Splore.
 *
 * Every setting is saved the moment it changes, to the same files launch.sh
 * reads, so there is no page for saving them. Like gfx.c, nothing here calls
 * SDL: sdl-display.c feeds it button presses and shows what it draws.
 */

#ifndef MENU_H
#define MENU_H

#include <stddef.h>
#include <stdint.h>

#include "gfx.h"

/* the buttons the menu reads, as bits of the masks menu_update takes */
enum {
  MENU_UP = 1 << 0,
  MENU_DOWN = 1 << 1,
  MENU_LEFT = 1 << 2,
  MENU_RIGHT = 1 << 3,
  MENU_A = 1 << 4,
  MENU_B = 1 << 5,
  MENU_MENU = 1 << 6,
  MENU_X = 1 << 7,
};

/* Save and Load leave the slot in menu->states.slot for sdl-display.c to carry out */
enum menu_result { MENU_STAY, MENU_CLOSE, MENU_QUIT, MENU_SAVE_STATE, MENU_LOAD_STATE };

/* which setting minarch's indicator shows, in place of the battery */
enum menu_indicator { MENU_INDICATOR_NONE, MENU_INDICATOR_BRIGHTNESS, MENU_INDICATOR_VOLUME };

/* the ranges libmsettings reports them in */
#define MENU_BRIGHTNESS_MAX 10
#define MENU_VOLUME_MAX 20

/*
 * Options lists the groups, as minarch's does, and each group but Shortcuts
 * opens a page of its settings
 */
enum menu_page { MENU_PAGE_ROOT, MENU_PAGE_OPTIONS, MENU_PAGE_GROUP, MENU_PAGE_SHORTCUTS };

enum menu_root_row { MENU_CONTINUE, MENU_SAVE, MENU_LOAD, MENU_OPTIONS, MENU_EXIT, MENU_ROOT_COUNT };

enum menu_group {
  MENU_GROUP_FRONTEND,
  MENU_GROUP_EMULATOR,
  MENU_GROUP_SPLORE,
  MENU_GROUP_SHORTCUTS,
  MENU_GROUP_COUNT,
};

/* every setting the groups hold, whichever group shows it */
enum menu_setting {
  MENU_SCREEN_MODE,
  MENU_OVERLAY,
  MENU_COPY_CARTS,
  MENU_SPLORE_ARTWORK,
  /* the PICO-8 settings follow, one each, in the order config_options in menu.c lists them */
  MENU_CONFIG_FIRST,
  MENU_SHOW_FPS = MENU_CONFIG_FIRST,
  MENU_VOLUME,
  MENU_BATTERY_SAVER,
  MENU_HIDE_MATURE,
  MENU_CHECK_UPDATES,
  MENU_SETTING_COUNT,
};

#define MENU_CONFIG_COUNT (MENU_SETTING_COUNT - MENU_CONFIG_FIRST)

/* how many settings a group shows, none for Shortcuts, and which is at a row */
int menu_group_size(int group);
int menu_group_setting(int group, int row);

/* what a shortcut does, one row each of the Shortcuts page */
enum menu_shortcut {
  MENU_SHORTCUT_SCREENSHOT,
  MENU_SHORTCUT_SAVE_GIF,
  MENU_SHORTCUT_QUIT,
  MENU_SHORTCUT_RESET,
  MENU_SHORTCUT_SCREEN_MODE,
  MENU_SHORTCUT_OVERLAY,
  MENU_SHORTCUT_CPU_METER,
  MENU_SHORTCUT_COUNT,
};

/*
 * A shortcut's button, as SDL numbers a game controller's, and whether Menu
 * has to be held with it, as NextUI's MENU+button shortcuts are. A button
 * of -1 is a shortcut nothing is bound to.
 */
struct menu_binding {
  int button;
  int with_menu;
};

/*
 * SDL reports L2 and R2 as the triggers' axes rather than as buttons, so
 * they are numbered after the last of SDL's buttons.
 */
#define MENU_BUTTON_L2 21
#define MENU_BUTTON_R2 22

/* the controller buttons a shortcut can use: A to Right, L2 and R2, all but Menu itself */
#define MENU_BINDABLE_BUTTONS 23
int menu_is_bindable(int button);

/* the values the screen-mode file holds, in the order the menu steps through */
extern const char *const menu_screen_modes[];
#define MENU_SCREEN_MODE_COUNT 2
#define MENU_SCREEN_MODE_STRETCHED 1

enum { MENU_COPY_OFF, MENU_COPY_ALL, MENU_COPY_FAVOURITES, MENU_COPY_COUNT };

/* the values the splore-artwork file holds, in the order the menu steps through */
extern const char *const menu_splore_artworks[];
#define MENU_SPLORE_ARTWORK_COUNT 2

/*
 * What the menu changes. Paths left NULL are neither read nor written. The
 * overlay names belong to sdl-display.c, and screen_mode and overlay are the
 * values it draws with, so a change shows as soon as the menu closes.
 */
struct menu_settings {
  const char *screen_mode_path;
  const char *overlay_path;
  /* the copy-carts markers are read from both folders and written to the first */
  const char *copy_carts_dir;
  const char *shared_copy_carts_dir;
  /* the Splore cover chosen, which the menu gives the Splore carts as it changes */
  const char *splore_artwork_path;
  /* the roms folders, the tag that marks this pak's, and the pak's two covers */
  const char *roms_dir;
  const char *tag;
  const char *splore_covers_dir;
  /* the PICO-8 settings the menu has changed, which launch.sh puts in config.txt */
  const char *pico8_config_path;
  /* the config.txt pico-8 was started with, for the settings never changed */
  const char *installed_config_path;

  /* the shortcuts' buttons, saved here as they change */
  const char *shortcuts_path;
  struct menu_binding shortcuts[MENU_SHORTCUT_COUNT];

  char *const *overlay_names;
  int overlay_count;

  int screen_mode;
  /* an index into overlay_names, or -1 for none */
  int overlay;
};

/* minarch's eight slots, as Save and Load step through them */
#define MENU_SLOTS 8

/*
 * The save state slots, which sdl-display.c fills in once menu_open has run,
 * as nothing here reads a state or decodes a picture.
 */
struct menu_states {
  /* whether states can be saved at all, which Save and Load need */
  int available;
  int slot;
  int exists[MENU_SLOTS];
  /* each slot's preview, 0xAARRGGBB, or NULL when it has none */
  const uint32_t *preview[MENU_SLOTS];
  int preview_w[MENU_SLOTS], preview_h[MENU_SLOTS];
};

struct menu {
  struct menu_settings *settings;
  const char *title;
  struct menu_states states;

  enum menu_page page;
  int root_row;
  /* the group selected on the Options page */
  int option_row;
  /* the first row on screen of each page, when they do not all fit */
  int option_top;
  /* the row selected on the open group's page, an index into the group */
  int setting_row;
  int setting_top;
  int shortcut_row;
  int shortcut_top;
  /* the shortcut waiting for a button to be pressed for it, or -1 */
  int binding;

  int copy_carts;
  int splore_artwork;
  int config[MENU_CONFIG_COUNT];

  /* the volume or brightness being changed, shown in place of the battery */
  enum menu_indicator indicator;
  int indicator_value;

  /* the direction held for repeating, and when it next repeats */
  unsigned repeat_button;
  uint32_t repeat_at;

  /* set whenever what menu_draw would show has changed */
  int dirty;
};

/* one line of a file, without its line ending, or empty when it cannot be read */
void menu_read_line(const char *path, char *line, size_t size);
void menu_write_line(const char *path, const char *line);

/* the saved screen mode as an index into menu_screen_modes, standard if unknown */
int menu_read_screen_mode(const char *path);
/* the saved overlay as an index into names, or -1 for none */
int menu_read_overlay(const char *path, char *const *names, int count);

/* the saved Splore artwork as an index into menu_splore_artworks, standard if unknown */
int menu_read_splore_artwork(const char *path);

/* starts on Continue, with the settings launch.sh applies read from disk */
void menu_open(struct menu *menu, struct menu_settings *settings, const char *title);

/*
 * pressed holds the buttons that went down since the last call and held the
 * ones down now, which is what Up, Down, Left and Right repeat from. now is a
 * millisecond clock.
 */
enum menu_result menu_update(struct menu *menu, unsigned pressed, unsigned held, uint32_t now);

/* the root page is drawn over frame, the game, which may be NULL for black */
void menu_draw(struct menu *menu, gfx_canvas *canvas, const uint32_t *frame, const gfx_font *font,
               const gfx_theme *theme, const gfx_battery *battery);

/* the size of minarch's volume and brightness indicator on a canvas this size */
void menu_indicator_size(int canvas_w, int canvas_h, int *w, int *h);

/*
 * Draws minarch's indicator at x, y, an icon and a bar showing value out of
 * the setting's range, laid out for a screen of screen_w by screen_h. The
 * game draws it over every frame while the setting changes.
 */
void menu_draw_indicator(gfx_canvas *canvas, int screen_w, int screen_h, const gfx_theme *theme,
                         int x, int y, enum menu_indicator indicator, int value);

/* the text a setting's row shows for its current value */
void menu_option_value(const struct menu *menu, int setting, char *out, size_t size);

/* reads the shortcuts saved at path, leaving every other one unbound */
void menu_read_shortcuts(const char *path, struct menu_binding *shortcuts);
/* the name a binding shows, such as A or MENU+A, or None */
void menu_binding_name(const struct menu_binding *binding, char *out, size_t size);
/* the name a shortcut shows, such as Screenshot */
const char *menu_shortcut_label(int shortcut);

/*
 * While a shortcut waits for its button, every button pressed is given here
 * rather than to menu_update, which binds it, unbinding any other shortcut
 * that used the same one, and saves them all.
 */
int menu_is_binding(const struct menu *menu);
void menu_bind(struct menu *menu, int button, int with_menu);

/* what the screen mode and overlay shortcuts do, saved as the menu saves them */
void menu_cycle_screen_mode(struct menu_settings *settings);
void menu_cycle_overlay(struct menu_settings *settings);

/*
 * A short message in a pill at the top right, as NextUI shows when a
 * screenshot is saved. The size depends on the text, so it takes the font.
 */
void menu_notification_size(int screen_w, int screen_h, const gfx_font *font, const char *text,
                            int *w, int *h);
void menu_draw_notification(gfx_canvas *canvas, int screen_w, int screen_h, const gfx_theme *theme,
                            const gfx_font *font, int x, int y, const char *text);

#endif
