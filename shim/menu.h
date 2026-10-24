/*
 * The in-game menu, laid out as minarch's: Continue, Save, Load, Options and
 * Quit over the dimmed game, with Save and Load shown but unavailable until
 * the pak has save states, and an Options page for the pak's settings and the
 * PICO-8 settings it writes into config.txt.
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
};

enum menu_result { MENU_STAY, MENU_CLOSE, MENU_QUIT };

/* which setting minarch's indicator shows, in place of the battery */
enum menu_indicator { MENU_INDICATOR_NONE, MENU_INDICATOR_BRIGHTNESS, MENU_INDICATOR_VOLUME };

/* the ranges libmsettings reports them in */
#define MENU_BRIGHTNESS_MAX 10
#define MENU_VOLUME_MAX 20

enum menu_page { MENU_PAGE_ROOT, MENU_PAGE_OPTIONS };

enum menu_root_row { MENU_CONTINUE, MENU_SAVE, MENU_LOAD, MENU_OPTIONS, MENU_EXIT, MENU_ROOT_COUNT };

enum menu_option_row {
  MENU_SCREEN_MODE,
  MENU_OVERLAY,
  MENU_COPY_CARTS,
  MENU_SPLORE_ARTWORK,
  /* the PICO-8 settings follow, one row each */
  MENU_CONFIG_FIRST,
};

#define MENU_CONFIG_COUNT 5
#define MENU_OPTION_COUNT (MENU_CONFIG_FIRST + MENU_CONFIG_COUNT)

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
  /* the cover launch.sh gives the Splore cart in the game list when it exits */
  const char *splore_artwork_path;
  /* the PICO-8 settings the menu has changed, which launch.sh puts in config.txt */
  const char *pico8_config_path;
  /* the config.txt pico-8 was started with, for the settings never changed */
  const char *installed_config_path;

  char *const *overlay_names;
  int overlay_count;

  int screen_mode;
  /* an index into overlay_names, or -1 for none */
  int overlay;
};

struct menu {
  struct menu_settings *settings;
  const char *title;

  enum menu_page page;
  int root_row;
  int option_row;
  /* the first option row on screen, when they do not all fit */
  int option_top;

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

/* the text an option row shows for its current value */
void menu_option_value(const struct menu *menu, int row, char *out, size_t size);

#endif
