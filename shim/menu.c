/*
 * The in-game menu. See menu.h.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "menu.h"

#define LOG(...) gfx_log(__VA_ARGS__)

/* how long a held direction waits before repeating, and then between repeats */
#define REPEAT_DELAY 400
#define REPEAT_RATE 55

/* what an unselected root row is dimmed to when it cannot be chosen yet */
#define DISABLED_TEXT GFX_GRAY

const char *const menu_screen_modes[] = {"standard", "stretched"};
const char *const menu_splore_artworks[] = {"standard", "fancy"};

static const char *const screen_mode_names[] = {"Standard", "Stretched"};
static const char *const splore_artwork_names[] = {"Standard", "Fancy"};
static const char *const copy_carts_names[] = {"Off", "All", "Favourites"};
static const char *const copy_carts_markers[] = {"copy-carts", "copy-carts-favourites",
                                                 "copy-carts-favorites"};
static const char *const off_on[] = {"Off", "On"};
static const char *const zero_one[] = {"0", "1"};
static const char *const volume_names[] = {"0%", "25%", "50%", "75%", "100%"};
static const char *const volume_values[] = {"0", "64", "128", "192", "256"};
static const char *const sleep_values[] = {"4", "10"};

static const char *const root_labels[] = {"Continue", "Save", "Load", "Options", "Quit"};

/* a description's note on when it applies goes on a line of its own */
#define NEXT_LAUNCH "\nApplies the next time PICO-8 starts."

/*
 * The config.txt keys the menu offers, the names it shows for each value and
 * the value written for each, and which value counts when neither the menu's
 * file nor config.txt names one, which matches every template in config/.
 */
static const struct config_option {
  const char *label;
  const char *key;
  const char *description;
  int count;
  const char *const *names;
  const char *const *values;
  int fallback;
} config_options[MENU_CONFIG_COUNT] = {
    {"Show FPS", "show_fps", "Draws the frame rate in the corner." NEXT_LAUNCH, 2, off_on,
     zero_one, 0},
    {"Volume", "volume", "PICO-8's own volume, under the device's." NEXT_LAUNCH, 5, volume_names,
     volume_values, 4},
    {"Battery Saver", "foreground_sleep_ms",
     "Sleeps longer between frames to save power." NEXT_LAUNCH, 2, off_on, sleep_values, 0},
    {"Hide Mature Carts", "splore_filter",
     "Hides carts the community tagged as mature in Splore." NEXT_LAUNCH, 2, off_on, zero_one, 0},
    {"Check Cart Updates", "check_for_cart_updates",
     "Checks for a newer version of a BBS cart each time it runs." NEXT_LAUNCH, 2, off_on,
     zero_one, 1},
};

static const char *const option_labels[MENU_CONFIG_FIRST] = {"Screen Mode", "Overlay",
                                                             "Copy Splore Carts", "Splore Artwork"};

static const char *const option_descriptions[MENU_CONFIG_FIRST] = {
    "Standard keeps the game square, Stretched fills the screen.",
    "A PNG from /Overlays/PICO drawn over the game in Standard mode.",
    "Copies the carts Splore downloaded into the roms folder.\nApplies when the cart exits.",
    "The cover the Splore cart shows in the game list.\nApplies when the cart exits.",
};

void menu_read_line(const char *path, char *line, size_t size)
{
  FILE *file = path != NULL ? fopen(path, "r") : NULL;

  line[0] = '\0';
  if (file != NULL) {
    if (fgets(line, (int)size, file) == NULL) {
      line[0] = '\0';
    }
    fclose(file);
  }
  line[strcspn(line, "\r\n")] = '\0';
}

void menu_write_line(const char *path, const char *line)
{
  FILE *file;

  if (path == NULL || *path == '\0') {
    return;
  }

  file = fopen(path, "w");
  if (file == NULL) {
    LOG("cannot save %s to %s\n", line, path);
    return;
  }

  fprintf(file, "%s\n", line);
  fclose(file);
}

int menu_read_screen_mode(const char *path)
{
  char line[64];
  int i;

  menu_read_line(path, line, sizeof line);
  for (i = 0; i < MENU_SCREEN_MODE_COUNT; i++) {
    if (strcmp(menu_screen_modes[i], line) == 0) {
      return i;
    }
  }

  return 0;
}

/* Nothing is drawn until an overlay has been chosen, or after its file is gone. */
int menu_read_overlay(const char *path, char *const *names, int count)
{
  char line[256];
  int i;

  menu_read_line(path, line, sizeof line);
  for (i = 0; i < count; i++) {
    if (strcmp(names[i], line) == 0) {
      return i;
    }
  }

  return -1;
}

int menu_read_splore_artwork(const char *path)
{
  char line[64];
  int i;

  menu_read_line(path, line, sizeof line);
  for (i = 0; i < MENU_SPLORE_ARTWORK_COUNT; i++) {
    if (strcmp(menu_splore_artworks[i], line) == 0) {
      return i;
    }
  }

  return 0;
}

static int marker_exists(const char *dir, const char *name)
{
  char path[4096];

  if (dir == NULL || *dir == '\0') {
    return 0;
  }

  snprintf(path, sizeof path, "%s/%s", dir, name);
  return access(path, F_OK) == 0;
}

/* read as launch.sh's copy_carts reads them: a favourites marker anywhere wins */
static int read_copy_carts(const struct menu_settings *settings)
{
  const char *dirs[] = {settings->copy_carts_dir, settings->shared_copy_carts_dir};
  int mode = MENU_COPY_OFF;
  size_t d;

  for (d = 0; d < sizeof dirs / sizeof *dirs; d++) {
    if (marker_exists(dirs[d], copy_carts_markers[1]) ||
        marker_exists(dirs[d], copy_carts_markers[2])) {
      return MENU_COPY_FAVOURITES;
    }
    if (marker_exists(dirs[d], copy_carts_markers[0])) {
      mode = MENU_COPY_ALL;
    }
  }

  return mode;
}

/* every marker goes from both folders, so the one written is the one that counts */
static void write_copy_carts(const struct menu_settings *settings, int mode)
{
  const char *dirs[] = {settings->copy_carts_dir, settings->shared_copy_carts_dir};
  char path[4096];
  size_t d, m;
  FILE *file;

  for (d = 0; d < sizeof dirs / sizeof *dirs; d++) {
    for (m = 0; m < sizeof copy_carts_markers / sizeof *copy_carts_markers; m++) {
      if (marker_exists(dirs[d], copy_carts_markers[m])) {
        snprintf(path, sizeof path, "%s/%s", dirs[d], copy_carts_markers[m]);
        if (unlink(path) != 0) {
          LOG("cannot remove %s\n", path);
        }
      }
    }
  }

  if (mode == MENU_COPY_OFF || settings->copy_carts_dir == NULL ||
      *settings->copy_carts_dir == '\0') {
    return;
  }

  snprintf(path, sizeof path, "%s/%s", settings->copy_carts_dir,
           copy_carts_markers[mode == MENU_COPY_ALL ? 0 : 1]);
  file = fopen(path, "w");
  if (file == NULL) {
    LOG("cannot create %s\n", path);
    return;
  }
  fclose(file);
}

/*
 * Finds key in a config.txt style file, where each setting is a line of the
 * key, a space and its value, maybe followed by a comment.
 */
static int read_config_value(const char *path, const char *key, char *value, size_t size)
{
  FILE *file = path != NULL ? fopen(path, "r") : NULL;
  size_t key_length = strlen(key);
  char line[512];
  int found = 0;

  if (file == NULL) {
    return 0;
  }

  while (fgets(line, sizeof line, file) != NULL) {
    if (strncmp(line, key, key_length) == 0 && line[key_length] == ' ') {
      const char *at = line + key_length;
      at += strspn(at, " ");
      snprintf(value, size, "%.*s", (int)strcspn(at, " \t\r\n"), at);
      found = 1;
    }
  }

  fclose(file);
  return found;
}

static int read_config(const struct menu_settings *settings, int index)
{
  const struct config_option *option = &config_options[index];
  char value[64];
  int i;

  if (!read_config_value(settings->pico8_config_path, option->key, value, sizeof value) &&
      !read_config_value(settings->installed_config_path, option->key, value, sizeof value)) {
    return option->fallback;
  }

  for (i = 0; i < option->count; i++) {
    if (strcmp(option->values[i], value) == 0) {
      return i;
    }
  }

  return option->fallback;
}

/* rewrites the file with key set to value, keeping the other keys it holds */
static void write_config(const struct menu_settings *settings, int index, int choice)
{
  const struct config_option *option = &config_options[index];
  const char *path = settings->pico8_config_path;
  size_t key_length = strlen(option->key);
  char lines[32][128];
  int count = 0, i, replaced = 0;
  FILE *file;

  if (path == NULL || *path == '\0') {
    return;
  }

  file = fopen(path, "r");
  if (file != NULL) {
    while (count < 32 && fgets(lines[count], sizeof lines[count], file) != NULL) {
      lines[count][strcspn(lines[count], "\r\n")] = '\0';
      if (lines[count][0] != '\0') {
        count++;
      }
    }
    fclose(file);
  }

  for (i = 0; i < count; i++) {
    if (strncmp(lines[i], option->key, key_length) == 0 && lines[i][key_length] == ' ') {
      snprintf(lines[i], sizeof lines[i], "%s %s", option->key, option->values[choice]);
      replaced = 1;
    }
  }
  if (!replaced && count < 32) {
    snprintf(lines[count++], sizeof lines[0], "%s %s", option->key, option->values[choice]);
  }

  file = fopen(path, "w");
  if (file == NULL) {
    LOG("cannot save %s to %s\n", option->key, path);
    return;
  }
  for (i = 0; i < count; i++) {
    fprintf(file, "%s\n", lines[i]);
  }
  fclose(file);
}

void menu_open(struct menu *menu, struct menu_settings *settings, const char *title)
{
  int i;

  memset(menu, 0, sizeof *menu);
  menu->settings = settings;
  menu->title = title != NULL && *title != '\0' ? title : "PICO-8";
  menu->page = MENU_PAGE_ROOT;
  menu->root_row = MENU_CONTINUE;
  menu->copy_carts = read_copy_carts(settings);
  menu->splore_artwork = menu_read_splore_artwork(settings->splore_artwork_path);
  for (i = 0; i < MENU_CONFIG_COUNT; i++) {
    menu->config[i] = read_config(settings, i);
  }
  menu->dirty = 1;
}

static int choice_count(const struct menu *menu, int row)
{
  switch (row) {
  case MENU_SCREEN_MODE:
    return MENU_SCREEN_MODE_COUNT;
  case MENU_OVERLAY:
    return menu->settings->overlay_count + 1;
  case MENU_COPY_CARTS:
    return MENU_COPY_COUNT;
  case MENU_SPLORE_ARTWORK:
    return MENU_SPLORE_ARTWORK_COUNT;
  default:
    return config_options[row - MENU_CONFIG_FIRST].count;
  }
}

/* None is the first overlay choice and each PNG follows, named without .png */
static void choice_name(const struct menu *menu, int row, int choice, char *out, size_t size)
{
  const char *name;
  size_t length;

  switch (row) {
  case MENU_SCREEN_MODE:
    snprintf(out, size, "%s", screen_mode_names[choice]);
    break;
  case MENU_OVERLAY:
    if (choice == 0) {
      snprintf(out, size, "None");
      break;
    }
    name = menu->settings->overlay_names[choice - 1];
    length = strlen(name);
    snprintf(out, size, "%.*s", (int)(length > 4 ? length - 4 : length), name);
    break;
  case MENU_COPY_CARTS:
    snprintf(out, size, "%s", copy_carts_names[choice]);
    break;
  case MENU_SPLORE_ARTWORK:
    snprintf(out, size, "%s", splore_artwork_names[choice]);
    break;
  default:
    snprintf(out, size, "%s", config_options[row - MENU_CONFIG_FIRST].names[choice]);
    break;
  }
}

static int current_choice(const struct menu *menu, int row)
{
  switch (row) {
  case MENU_SCREEN_MODE:
    return menu->settings->screen_mode;
  case MENU_OVERLAY:
    return menu->settings->overlay + 1;
  case MENU_COPY_CARTS:
    return menu->copy_carts;
  case MENU_SPLORE_ARTWORK:
    return menu->splore_artwork;
  default:
    return menu->config[row - MENU_CONFIG_FIRST];
  }
}

void menu_option_value(const struct menu *menu, int row, char *out, size_t size)
{
  choice_name(menu, row, current_choice(menu, row), out, size);
}

static void step_option(struct menu *menu, int row, int step)
{
  struct menu_settings *settings = menu->settings;
  int count = choice_count(menu, row);
  int choice = (current_choice(menu, row) + step + count) % count;

  switch (row) {
  case MENU_SCREEN_MODE:
    settings->screen_mode = choice;
    LOG("switched to %s screen mode\n", menu_screen_modes[choice]);
    menu_write_line(settings->screen_mode_path, menu_screen_modes[choice]);
    break;
  case MENU_OVERLAY:
    settings->overlay = choice - 1;
    LOG("switched to %s\n", choice == 0 ? "disabled" : settings->overlay_names[choice - 1]);
    menu_write_line(settings->overlay_path,
                    choice == 0 ? "disabled" : settings->overlay_names[choice - 1]);
    break;
  case MENU_COPY_CARTS:
    menu->copy_carts = choice;
    LOG("set Copy Splore Carts to %s\n", copy_carts_names[choice]);
    write_copy_carts(settings, choice);
    break;
  case MENU_SPLORE_ARTWORK:
    menu->splore_artwork = choice;
    LOG("set Splore Artwork to %s\n", menu_splore_artworks[choice]);
    menu_write_line(settings->splore_artwork_path, menu_splore_artworks[choice]);
    break;
  default:
    menu->config[row - MENU_CONFIG_FIRST] = choice;
    LOG("set %s to %s\n", config_options[row - MENU_CONFIG_FIRST].key,
        config_options[row - MENU_CONFIG_FIRST].values[choice]);
    write_config(settings, row - MENU_CONFIG_FIRST, choice);
    break;
  }
}

static int root_row_enabled(int row)
{
  return row != MENU_SAVE && row != MENU_LOAD;
}

/* Up and Down wrap, and on the root page pass over the rows that cannot be chosen */
static void move_row(struct menu *menu, int step)
{
  if (menu->page == MENU_PAGE_ROOT) {
    do {
      menu->root_row = (menu->root_row + step + MENU_ROOT_COUNT) % MENU_ROOT_COUNT;
    } while (!root_row_enabled(menu->root_row));
  } else {
    menu->option_row = (menu->option_row + step + MENU_OPTION_COUNT) % MENU_OPTION_COUNT;
  }
}

/* a direction held past the delay counts as pressed again at every repeat */
static unsigned add_repeats(struct menu *menu, unsigned pressed, unsigned held, uint32_t now)
{
  unsigned directions = MENU_UP | MENU_DOWN | MENU_LEFT | MENU_RIGHT;
  unsigned fresh = pressed & directions;

  if (fresh) {
    menu->repeat_button = fresh & -fresh;
    menu->repeat_at = now + REPEAT_DELAY;
    return pressed;
  }

  if (menu->repeat_button == 0 || !(held & menu->repeat_button)) {
    menu->repeat_button = 0;
    return pressed;
  }

  if ((int32_t)(now - menu->repeat_at) < 0) {
    return pressed;
  }

  menu->repeat_at = now + REPEAT_RATE;
  return pressed | menu->repeat_button;
}

enum menu_result menu_update(struct menu *menu, unsigned pressed, unsigned held, uint32_t now)
{
  pressed = add_repeats(menu, pressed, held, now);

  if (pressed & MENU_MENU) {
    return MENU_CLOSE;
  }

  if (pressed & (MENU_UP | MENU_DOWN)) {
    move_row(menu, pressed & MENU_UP ? -1 : 1);
    menu->dirty = 1;
  }

  if (menu->page == MENU_PAGE_ROOT) {
    if (pressed & MENU_B) {
      return MENU_CLOSE;
    }
    if (!(pressed & MENU_A)) {
      return MENU_STAY;
    }

    switch (menu->root_row) {
    case MENU_CONTINUE:
      return MENU_CLOSE;
    case MENU_OPTIONS:
      menu->page = MENU_PAGE_OPTIONS;
      menu->option_row = 0;
      menu->option_top = 0;
      menu->dirty = 1;
      return MENU_STAY;
    case MENU_EXIT:
      return MENU_QUIT;
    default:
      return MENU_STAY;
    }
  }

  if (pressed & MENU_B) {
    menu->page = MENU_PAGE_ROOT;
    menu->dirty = 1;
    return MENU_STAY;
  }

  if (pressed & (MENU_LEFT | MENU_RIGHT | MENU_A)) {
    step_option(menu, menu->option_row, pressed & MENU_LEFT ? -1 : 1);
    menu->dirty = 1;
  }

  return MENU_STAY;
}

/* minarch's title: the name in a pill at the top left, cut to fit */
static int draw_title(gfx_canvas *canvas, const gfx_metrics *m, const gfx_font *font,
                      const gfx_theme *theme, const char *text, int max_w)
{
  int px = gfx_u(m, GFX_FONT_LARGE), padding = gfx_u(m, GFX_BUTTON_PADDING);
  int x = gfx_u(m, m->padding), y = gfx_u(m, m->padding), h = gfx_u(m, GFX_PILL_SIZE);
  char shown[256];
  int w;

  if (max_w <= 2 * padding) {
    return x;
  }

  gfx_text_fit(font, px, text, max_w - 2 * padding, shown, sizeof shown);
  w = gfx_text_width(font, px, shown) + 2 * padding;
  w = w > max_w ? max_w : w;
  gfx_pill(canvas, x, y, w, h, gfx_title_pill(theme));
  gfx_text_draw(canvas, font, x + padding, y + (h - gfx_line_height(font, px)) / 2, px,
                gfx_title_text(theme), shown);
  return x + w;
}

/*
 * minarch's battery at the top right, a pill holding the outline of a battery
 * filled from the right. It turns red when low and shows a bolt on charge.
 * Returns the width it took, which is none without a battery.
 */
static int draw_battery(gfx_canvas *canvas, const gfx_metrics *m, const gfx_theme *theme,
                        const gfx_battery *battery)
{
  int size = gfx_u(m, GFX_PILL_SIZE);
  int x = canvas->w - gfx_u(m, m->padding) - size, y = gfx_u(m, m->padding);
  int bx = x + (size - gfx_u(m, 17)) / 2, by = y + (size - gfx_u(m, 10)) / 2;
  int fx = bx + gfx_u(m, 2), fy = by + gfx_u(m, 2), fw = gfx_u(m, 11), fh = gfx_u(m, 6);
  uint32_t background = gfx_group_pill(theme);
  uint32_t edge;
  int filled;

  if (battery == NULL || battery->percent < 0) {
    return 0;
  }

  edge = !battery->charging && battery->percent <= 10 ? GFX_RED : GFX_WHITE;
  gfx_pill(canvas, x, y, size, size, background);
  gfx_rounded(canvas, bx, by, gfx_u(m, 15), gfx_u(m, 10), gfx_u(m, 2), edge);
  gfx_rounded(canvas, bx + gfx_u(m, 1), by + gfx_u(m, 1), gfx_u(m, 13), gfx_u(m, 8), gfx_u(m, 1),
              background);
  gfx_fill(canvas, bx + gfx_u(m, 15), by + gfx_u(m, 3), gfx_u(m, 2), gfx_u(m, 4), edge);

  if (battery->charging) {
    /* a bolt cut out of a full battery, as two overlapping triangles */
    gfx_fill(canvas, fx, fy, fw, fh, GFX_WHITE);
    gfx_triangle(canvas, fx + fw * 13 / 20, fy, fx + fw * 5 / 20, fy + fh * 12 / 20,
                 fx + fw * 11 / 20, fy + fh * 11 / 20, background);
    gfx_triangle(canvas, fx + fw * 9 / 20, fy + fh * 9 / 20, fx + fw * 15 / 20, fy + fh * 8 / 20,
                 fx + fw * 7 / 20, fy + fh, background);
    return size;
  }

  filled = fw * battery->percent / 100;
  if (filled > 0) {
    gfx_fill(canvas, fx + fw - filled, fy, filled, fh, battery->percent <= 20 ? GFX_RED : GFX_WHITE);
  }
  return size;
}

/* minarch's speaker, 18x16 units, or only its cone when muted */
static void draw_speaker(gfx_canvas *canvas, const gfx_metrics *m, int x, int y, int muted,
                         uint32_t colour)
{
  gfx_fill(canvas, x, y + gfx_u(m, 5), gfx_u(m, 4), gfx_u(m, 6), colour);
  gfx_triangle(canvas, x + gfx_u(m, 3), y + gfx_u(m, 5), x + gfx_u(m, 10), y, x + gfx_u(m, 10),
               y + gfx_u(m, 16), colour);
  gfx_triangle(canvas, x + gfx_u(m, 3), y + gfx_u(m, 5), x + gfx_u(m, 10), y + gfx_u(m, 16),
               x + gfx_u(m, 3), y + gfx_u(m, 11), colour);
  if (!muted) {
    gfx_arc(canvas, x + gfx_u(m, 10), y + gfx_u(m, 8), gfx_u(m, 4), gfx_u(m, 2), colour);
    gfx_arc(canvas, x + gfx_u(m, 10), y + gfx_u(m, 8), gfx_u(m, 8), gfx_u(m, 2), colour);
  }
}

/* minarch's sun, 19x19 units: a disc and eight dots around it */
static void draw_sun(gfx_canvas *canvas, const gfx_metrics *m, int x, int y, uint32_t colour)
{
  /* the eight directions, in tenths of a unit vector */
  static const int directions[8][2] = {{10, 0}, {7, 7}, {0, 10}, {-7, 7},
                                       {-10, 0}, {-7, -7}, {0, -10}, {7, -7}};
  int cx = x + gfx_u(m, 9.5), cy = y + gfx_u(m, 9.5);
  int i;

  gfx_circle(canvas, cx, cy, gfx_u(m, 4.5), colour);
  for (i = 0; i < 8; i++) {
    gfx_circle(canvas, cx + gfx_u(m, 8) * directions[i][0] / 10,
               cy + gfx_u(m, 8) * directions[i][1] / 10, gfx_u(m, 1.5), colour);
  }
}

void menu_indicator_size(int canvas_w, int canvas_h, int *w, int *h)
{
  gfx_metrics m = gfx_metrics_for(canvas_w, canvas_h);

  *w = gfx_u(&m, GFX_PILL_SIZE + GFX_SETTINGS_WIDTH + 14);
  *h = gfx_u(&m, GFX_PILL_SIZE);
}

/*
 * minarch's indicator: a pill holding the setting's icon and a bar filled to
 * its value. Volume at zero shows the muted speaker and no bar.
 */
void menu_draw_indicator(gfx_canvas *canvas, int screen_w, int screen_h, const gfx_theme *theme,
                         int x, int y, enum menu_indicator indicator, int value)
{
  gfx_metrics m = gfx_metrics_for(screen_w, screen_h);
  int max = indicator == MENU_INDICATOR_BRIGHTNESS ? MENU_BRIGHTNESS_MAX : MENU_VOLUME_MAX;
  int pill = gfx_u(&m, GFX_PILL_SIZE), w, h, bar_x, bar_y, bar_w, bar_h;

  menu_indicator_size(screen_w, screen_h, &w, &h);
  value = value < 0 ? 0 : value > max ? max : value;
  gfx_pill(canvas, x, y, w, h, gfx_indicator_pill(theme));

  if (indicator == MENU_INDICATOR_BRIGHTNESS) {
    draw_sun(canvas, &m, x + (pill - gfx_u(&m, 19)) / 2, y + (pill - gfx_u(&m, 19)) / 2,
             gfx_indicator_icon(theme));
  } else {
    draw_speaker(canvas, &m, x + (pill - gfx_u(&m, value > 0 ? 18 : 10)) / 2,
                 y + (pill - gfx_u(&m, 16)) / 2, value == 0, gfx_indicator_icon(theme));
  }

  bar_x = x + pill;
  bar_y = y + gfx_u(&m, (GFX_PILL_SIZE - GFX_SETTINGS_SIZE) / 2.0);
  bar_w = gfx_u(&m, GFX_SETTINGS_WIDTH);
  bar_h = gfx_u(&m, GFX_SETTINGS_SIZE);
  gfx_pill(canvas, bar_x, bar_y, bar_w, bar_h, gfx_indicator_track(theme));
  if (value > 0) {
    gfx_pill(canvas, bar_x, bar_y, bar_w * value / max, bar_h, gfx_indicator_bar(theme));
  }
}

/* the battery at the top right, or the indicator while a setting changes */
static int draw_hardware(struct menu *menu, gfx_canvas *canvas, const gfx_metrics *m,
                         const gfx_theme *theme, const gfx_battery *battery)
{
  int w, h;

  if (menu->indicator == MENU_INDICATOR_NONE) {
    return draw_battery(canvas, m, theme, battery);
  }

  menu_indicator_size(canvas->w, canvas->h, &w, &h);
  menu_draw_indicator(canvas, canvas->w, canvas->h, theme, canvas->w - gfx_u(m, m->padding) - w,
                      gfx_u(m, m->padding), menu->indicator, menu->indicator_value);
  return w;
}

struct button_hint {
  const char *button;
  const char *hint;
};

static int button_width(const gfx_metrics *m, const gfx_font *font, const char *button)
{
  int size = gfx_u(m, GFX_BUTTON_SIZE);

  return strlen(button) == 1 ? size : size / 2 + gfx_text_width(font, gfx_u(m, GFX_FONT_TINY), button);
}

/*
 * minarch's button hints along the bottom edge: a pill holding each button,
 * a circle for a single letter and a pill for a longer name, and its hint.
 */
static void draw_buttons(gfx_canvas *canvas, const gfx_metrics *m, const gfx_font *font,
                         const gfx_theme *theme, const struct button_hint *hints, int count,
                         int right)
{
  int size = gfx_u(m, GFX_BUTTON_SIZE), margin = gfx_u(m, GFX_BUTTON_MARGIN);
  int tiny = gfx_u(m, GFX_FONT_TINY), small = gfx_u(m, GFX_FONT_SMALL);
  int medium = gfx_u(m, GFX_FONT_MEDIUM);
  int total = margin, x, y, i;

  for (i = 0; i < count; i++) {
    total += button_width(m, font, hints[i].button) + margin +
             gfx_text_width(font, small, hints[i].hint) + 2 * margin;
  }

  x = right ? canvas->w - gfx_u(m, m->padding) - total : gfx_u(m, m->padding);
  y = canvas->h - gfx_u(m, m->padding + GFX_PILL_SIZE);
  gfx_pill(canvas, x, y, total, gfx_u(m, GFX_PILL_SIZE), gfx_group_pill(theme));

  x += margin;
  y += margin;
  for (i = 0; i < count; i++) {
    const char *button = hints[i].button;
    int w = button_width(m, font, button);

    if (strlen(button) == 1) {
      gfx_circle(canvas, x + size / 2, y + size / 2, size / 2, GFX_WHITE);
      gfx_text_draw(canvas, font, x + (size - gfx_text_width(font, medium, button)) / 2,
                    y + (size - gfx_line_height(font, medium)) / 2, medium, GFX_GRAY, button);
    } else {
      gfx_pill(canvas, x, y, w, size, GFX_WHITE);
      gfx_text_draw(canvas, font, x + size / 4, y + (size - gfx_line_height(font, tiny)) / 2, tiny,
                    GFX_GRAY, button);
    }
    x = gfx_text_draw(canvas, font, x + w + margin, y + (size - gfx_line_height(font, small)) / 2,
                      small, gfx_hint_text(theme), hints[i].hint);
    x += 2 * margin;
  }
}

static void draw_root(struct menu *menu, gfx_canvas *canvas, const gfx_metrics *m,
                      const uint32_t *frame, const gfx_font *font, const gfx_theme *theme,
                      const gfx_battery *battery)
{
  static const struct button_hint sleep[] = {{"POWER", "SLEEP"}};
  static const struct button_hint actions[] = {{"B", "BACK"}, {"A", "OKAY"}};
  int pill = gfx_u(m, GFX_PILL_SIZE), padding = gfx_u(m, GFX_BUTTON_PADDING);
  int px = gfx_u(m, GFX_FONT_LARGE), line = gfx_line_height(font, px);
  int x = gfx_u(m, m->padding);
  int top = (canvas->h - gfx_u(m, m->padding * 2) - MENU_ROOT_COUNT * pill) / 2 +
            gfx_u(m, m->padding);
  int battery_w, row;

  if (frame != NULL) {
    gfx_dim(canvas, frame, 0x80);
  } else {
    gfx_clear(canvas, GFX_BLACK);
  }

  battery_w = draw_hardware(menu, canvas, m, theme, battery);
  draw_title(canvas, m, font, theme, menu->title,
             canvas->w - gfx_u(m, m->padding * 2) - (battery_w ? battery_w + gfx_u(m, m->padding) : 0));
  draw_buttons(canvas, m, font, theme, sleep, 1, 0);
  draw_buttons(canvas, m, font, theme, actions, 2, 1);

  for (row = 0; row < MENU_ROOT_COUNT; row++) {
    const char *label = root_labels[row];
    int y = top + row * pill, text_y = y + (pill - line) / 2;

    if (row == menu->root_row) {
      gfx_pill(canvas, x, y, gfx_text_width(font, px, label) + 2 * padding, pill,
               gfx_selected_pill(theme));
      gfx_text_draw(canvas, font, x + padding, text_y, px, gfx_selected_text(theme), label);
      continue;
    }

    if (gfx_shadow(theme)) {
      gfx_text_draw(canvas, font, x + padding + gfx_u(m, 2), text_y + gfx_u(m, 1), px, GFX_BLACK,
                    label);
    }
    gfx_text_draw(canvas, font, x + padding, text_y, px,
                  root_row_enabled(row) ? gfx_text(theme) : DISABLED_TEXT, label);
  }
}

/*
 * Splits text into up to two lines of at most max_w, at its line break when it
 * has one and otherwise at a space, cutting whatever still does not fit.
 */
static int wrap_description(const gfx_font *font, int px, const char *text, int max_w,
                            char lines[2][256])
{
  const char *newline = strchr(text, '\n');
  size_t cut = strlen(text), space;
  char first[256];

  if (newline != NULL) {
    snprintf(first, sizeof first, "%.*s", (int)(newline - text), text);
    gfx_text_fit(font, px, first, max_w, lines[0], sizeof lines[0]);
    gfx_text_fit(font, px, newline + 1, max_w, lines[1], sizeof lines[1]);
    return 2;
  }

  snprintf(lines[0], sizeof lines[0], "%s", text);
  lines[1][0] = '\0';
  while (gfx_text_width(font, px, lines[0]) > max_w) {
    space = cut;
    while (space > 0 && text[space - 1] != ' ') {
      space--;
    }
    if (space <= 1) {
      gfx_text_fit(font, px, text, max_w, lines[0], sizeof lines[0]);
      return 1;
    }
    cut = space - 1;
    snprintf(lines[0], sizeof lines[0], "%.*s", (int)cut, text);
  }

  if (text[cut] == '\0') {
    return 1;
  }
  gfx_text_fit(font, px, text + cut + 1, max_w, lines[1], sizeof lines[1]);
  return 2;
}

static const char *option_label(int row)
{
  return row < MENU_CONFIG_FIRST ? option_labels[row] : config_options[row - MENU_CONFIG_FIRST].label;
}

static const char *option_description(int row)
{
  return row < MENU_CONFIG_FIRST ? option_descriptions[row]
                                 : config_options[row - MENU_CONFIG_FIRST].description;
}

/*
 * minarch's option list: rows a button tall under the title, centred and as
 * wide as the widest label and value need, so stepping a value never resizes
 * the list. The selected row is a dark pill with a light one around its name,
 * values sit at the right in the tiny font, arrows show more rows above or
 * below, and the selected row's description is at the foot.
 */
static void draw_options(struct menu *menu, gfx_canvas *canvas, const gfx_metrics *m,
                         const gfx_font *font, const gfx_theme *theme, const gfx_battery *battery)
{
  int row_h = gfx_u(m, GFX_BUTTON_SIZE), op = gfx_u(m, GFX_OPTION_PADDING);
  int head = gfx_u(m, m->padding + GFX_PILL_SIZE) + gfx_u(m, GFX_BUTTON_MARGIN);
  int small = gfx_u(m, GFX_FONT_SMALL), tiny = gfx_u(m, GFX_FONT_TINY);
  int leading = gfx_u(m, 12), margin = gfx_u(m, m->padding);
  int desc_top = canvas->h - margin - 2 * leading;
  int visible = (desc_top - row_h - head) / row_h;
  int widest = 0, battery_w, row, choice, i, count, arrow_w, arrow_h, x;
  char full[256], value[256], label[256], lines[2][256];

  if (visible < 1) {
    visible = 1;
  }
  if (menu->option_row < menu->option_top) {
    menu->option_top = menu->option_row;
  }
  if (menu->option_row >= menu->option_top + visible) {
    menu->option_top = menu->option_row - visible + 1;
  }
  if (menu->option_top > MENU_OPTION_COUNT - visible) {
    menu->option_top = MENU_OPTION_COUNT - visible > 0 ? MENU_OPTION_COUNT - visible : 0;
  }

  gfx_clear(canvas, gfx_background(theme));
  battery_w = draw_hardware(menu, canvas, m, theme, battery);
  draw_title(canvas, m, font, theme, "Options",
             canvas->w - margin * 2 - (battery_w ? battery_w + margin : 0));

  for (row = 0; row < MENU_OPTION_COUNT; row++) {
    for (choice = 0; choice < choice_count(menu, row); choice++) {
      int w;

      choice_name(menu, row, choice, value, sizeof value);
      w = gfx_text_width(font, small, option_label(row)) + gfx_text_width(font, tiny, value) + 4 * op;
      widest = w > widest ? w : widest;
    }
  }
  if (widest > canvas->w - margin * 2) {
    widest = canvas->w - margin * 2;
  }
  x = (canvas->w - widest) / 2;

  for (i = 0; i < visible && menu->option_top + i < MENU_OPTION_COUNT; i++) {
    int at = menu->option_top + i;
    int y = head + i * row_h;
    int selected = at == menu->option_row;
    int value_w;

    menu_option_value(menu, at, full, sizeof full);
    gfx_text_fit(font, tiny, full, widest / 2, value, sizeof value);
    value_w = gfx_text_width(font, tiny, value);
    gfx_text_fit(font, small, option_label(at), widest - value_w - 3 * op, label, sizeof label);

    if (selected) {
      gfx_pill(canvas, x, y, widest, row_h, gfx_option_row(theme));
      gfx_pill(canvas, x, y, gfx_text_width(font, small, label) + 2 * op, row_h,
               gfx_selected_pill(theme));
    }
    gfx_text_draw(canvas, font, x + op, y + (row_h - gfx_line_height(font, small)) / 2, small,
                  selected ? gfx_selected_text(theme) : gfx_text(theme), label);
    gfx_text_draw(canvas, font, x + widest - value_w - op,
                  y + (row_h - gfx_line_height(font, tiny)) / 2, tiny,
                  selected ? GFX_WHITE : gfx_text(theme), value);
  }

  arrow_w = gfx_u(m, 24);
  arrow_h = gfx_u(m, GFX_SCROLL_HEIGHT) > 2 ? gfx_u(m, GFX_SCROLL_HEIGHT) : 2;
  if (menu->option_top > 0) {
    gfx_arrow(canvas, (canvas->w - arrow_w) / 2, head - gfx_u(m, GFX_BUTTON_MARGIN) - arrow_h, arrow_w,
              arrow_h, 0, GFX_GRAY);
  }
  if (menu->option_top + visible < MENU_OPTION_COUNT) {
    gfx_arrow(canvas, (canvas->w - arrow_w) / 2, head + visible * row_h + (row_h - arrow_h) / 2,
              arrow_w, arrow_h, 1, GFX_GRAY);
  }

  count = wrap_description(font, tiny, option_description(menu->option_row), canvas->w - margin * 2,
                           lines);
  for (i = 0; i < count; i++) {
    gfx_text_draw(canvas, font, (canvas->w - gfx_text_width(font, tiny, lines[i])) / 2,
                  desc_top + i * leading, tiny, gfx_hint_text(theme), lines[i]);
  }
}

void menu_draw(struct menu *menu, gfx_canvas *canvas, const uint32_t *frame, const gfx_font *font,
               const gfx_theme *theme, const gfx_battery *battery)
{
  gfx_metrics metrics = gfx_metrics_for(canvas->w, canvas->h);

  if (menu->page == MENU_PAGE_ROOT) {
    draw_root(menu, canvas, &metrics, frame, font, theme, battery);
  } else {
    draw_options(menu, canvas, &metrics, font, theme, battery);
  }
  menu->dirty = 0;
}
