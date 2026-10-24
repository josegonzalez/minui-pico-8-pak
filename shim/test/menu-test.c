/*
 * Checks the menu's pages, the files its settings write and its drawing,
 * linked straight against menu.c and gfx.c with no SDL at all:
 *
 *   menu-test MINUI_FONT NEXTUI_FONT
 *
 * The fonts are MinUI's BPreplayBold-unhinted.otf and NextUI's font1.ttf,
 * which shim/Dockerfile fetches for the test alone, as neither may ship with
 * the pak. Prints TAP and exits non-zero on a failure.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../cart.h"
#include "../gfx.h"
#include "../menu.h"

/* sizes every supported device draws at, including the Smart Pro and the cube */
static const int sizes[][2] = {{640, 480}, {720, 720}, {720, 480}, {1024, 768}, {1280, 720}};

/* written around each canvas to catch drawing outside it */
#define GUARD 4096
#define GUARD_VALUE 0x5a5a5a5au

static int tests;
static int failures;
static char dir[] = "/tmp/menu-test.XXXXXX";
static char userdata[4096];
static char shared[4096];
static char screen_mode_path[4096];
static char overlay_path[4096];
static char pico8_config_path[4096];
static char installed_config_path[4096];
static char splore_artwork_path[4096];
static char *overlay_names[] = {"a.png", "b.png"};

static gfx_canvas guarded_canvas(int w, int h);

static void check(int passed, const char *name)
{
  tests++;
  if (!passed) {
    failures++;
  }
  printf("%s %d - %s\n", passed ? "ok" : "not ok", tests, name);
}

static void write_file(const char *path, const char *text)
{
  FILE *file = fopen(path, "w");

  if (file == NULL || fputs(text, file) < 0) {
    perror(path);
    exit(2);
  }
  fclose(file);
}

static int file_is(const char *path, const char *expected)
{
  char text[1024] = "";
  FILE *file = fopen(path, "r");
  size_t got;

  if (file == NULL) {
    return 0;
  }
  got = fread(text, 1, sizeof text - 1, file);
  text[got] = '\0';
  fclose(file);
  return strcmp(text, expected) == 0;
}

static int exists(const char *folder, const char *name)
{
  char path[8192];

  snprintf(path, sizeof path, "%s/%s", folder, name);
  return access(path, F_OK) == 0;
}

static void make_dir(const char *path)
{
  if (mkdir(path, 0755) != 0) {
    perror(path);
    exit(2);
  }
}

static void make_fixtures(void)
{
  if (mkdtemp(dir) == NULL) {
    perror("mkdtemp");
    exit(2);
  }

  snprintf(userdata, sizeof userdata, "%s/userdata", dir);
  make_dir(userdata);
  snprintf(shared, sizeof shared, "%s/shared", dir);
  make_dir(shared);
  snprintf(screen_mode_path, sizeof screen_mode_path, "%s/screen-mode", dir);
  snprintf(overlay_path, sizeof overlay_path, "%s/overlay", dir);
  snprintf(pico8_config_path, sizeof pico8_config_path, "%s/pico8-config", dir);
  snprintf(installed_config_path, sizeof installed_config_path, "%s/config.txt", dir);
  snprintf(splore_artwork_path, sizeof splore_artwork_path, "%s/splore-artwork", dir);
}

static void settings_for_test(struct menu_settings *settings)
{
  memset(settings, 0, sizeof *settings);
  settings->screen_mode_path = screen_mode_path;
  settings->overlay_path = overlay_path;
  settings->copy_carts_dir = userdata;
  settings->shared_copy_carts_dir = shared;
  settings->pico8_config_path = pico8_config_path;
  settings->installed_config_path = installed_config_path;
  settings->splore_artwork_path = splore_artwork_path;
  settings->overlay_names = overlay_names;
  settings->overlay_count = 2;
  settings->overlay = -1;
}

/* presses and releases buttons, as one step of a thumb */
static enum menu_result press(struct menu *menu, unsigned buttons)
{
  enum menu_result result = menu_update(menu, buttons, buttons, 0);

  menu_update(menu, 0, 0, 0);
  return result;
}

static void test_root(void)
{
  struct menu_settings settings;
  struct menu menu;

  settings_for_test(&settings);
  menu_open(&menu, &settings, "");
  check(menu.page == MENU_PAGE_ROOT && menu.root_row == MENU_CONTINUE && menu.dirty,
        "the menu opens on Continue");
  check(strcmp(menu.title, "PICO-8") == 0, "a cart with no name is titled PICO-8");

  press(&menu, MENU_DOWN);
  check(menu.root_row == MENU_OPTIONS, "Down passes over Save and Load to Options");
  press(&menu, MENU_UP);
  check(menu.root_row == MENU_CONTINUE, "Up passes over them back to Continue");
  press(&menu, MENU_UP);
  check(menu.root_row == MENU_EXIT, "Up from Continue wraps to Quit");
  press(&menu, MENU_DOWN);
  check(menu.root_row == MENU_CONTINUE, "Down from Quit wraps to Continue");

  check(press(&menu, MENU_A) == MENU_CLOSE, "A on Continue closes the menu");
  check(press(&menu, MENU_B) == MENU_CLOSE, "B closes the menu");
  check(press(&menu, MENU_MENU) == MENU_CLOSE, "Menu closes the menu");
  press(&menu, MENU_UP);
  check(press(&menu, MENU_A) == MENU_QUIT, "A on Quit quits");

  press(&menu, MENU_DOWN);
  press(&menu, MENU_DOWN);
  menu.dirty = 0;
  check(press(&menu, MENU_A) == MENU_STAY && menu.page == MENU_PAGE_OPTIONS && menu.dirty &&
            menu.option_row == 0,
        "A on Options opens the Options page at its first row");
  check(press(&menu, MENU_MENU) == MENU_CLOSE, "Menu closes the menu from Options too");
  check(press(&menu, MENU_B) == MENU_STAY && menu.page == MENU_PAGE_ROOT &&
            menu.root_row == MENU_OPTIONS,
        "B on Options goes back to the root page on Options");
}

static void test_repeat(void)
{
  struct menu_settings settings;
  struct menu menu;

  settings_for_test(&settings);
  menu_open(&menu, &settings, "Cart");
  press(&menu, MENU_DOWN);
  press(&menu, MENU_A);

  menu_update(&menu, MENU_DOWN, MENU_DOWN, 1000);
  check(menu.option_row == 1, "a press of Down moves once");
  menu_update(&menu, 0, MENU_DOWN, 1399);
  check(menu.option_row == 1, "a held Down waits before repeating");
  menu_update(&menu, 0, MENU_DOWN, 1400);
  check(menu.option_row == 2, "a held Down repeats after 400ms");
  menu_update(&menu, 0, MENU_DOWN, 1454);
  check(menu.option_row == 2, "and not again within 55ms");
  menu_update(&menu, 0, MENU_DOWN, 1455);
  check(menu.option_row == 3, "then every 55ms");
  menu_update(&menu, 0, 0, 2000);
  menu_update(&menu, 0, MENU_DOWN, 2001);
  check(menu.option_row == 3, "a Down already down when the repeat stopped does not repeat");
}

static struct menu open_options(struct menu_settings *settings)
{
  struct menu menu;

  menu_open(&menu, settings, "Cart");
  press(&menu, MENU_DOWN);
  press(&menu, MENU_A);
  return menu;
}

/* opens the page of the group a setting is in, from Options or another group, and selects it */
static void select_setting(struct menu *menu, int setting)
{
  int group, row, found_group = -1, found_row = -1;

  for (group = 0; group < MENU_GROUP_COUNT; group++) {
    for (row = 0; row < menu_group_size(menu, group); row++) {
      if (menu_group_setting(menu, group, row) == setting) {
        found_group = group;
        found_row = row;
      }
    }
  }

  if (menu->page == MENU_PAGE_GROUP && menu->option_row != found_group) {
    press(menu, MENU_B);
  }
  if (menu->page != MENU_PAGE_GROUP) {
    while (menu->option_row != found_group) {
      press(menu, MENU_DOWN);
    }
    press(menu, MENU_A);
  }
  while (menu->setting_row != found_row) {
    press(menu, MENU_DOWN);
  }
}

static void test_screen_mode_and_overlay(void)
{
  struct menu_settings settings;
  struct menu menu;
  char value[64];

  settings_for_test(&settings);
  menu = open_options(&settings);
  check(menu.page == MENU_PAGE_OPTIONS && menu.option_row == MENU_GROUP_FRONTEND,
        "Options opens on its first group, Frontend");
  check(press(&menu, MENU_RIGHT) == MENU_STAY && menu.page == MENU_PAGE_GROUP &&
            menu.setting_row == 0 &&
            menu_group_setting(&menu, MENU_GROUP_FRONTEND, 0) == MENU_SCREEN_MODE,
        "Right opens Frontend at its first setting, Screen Mode");
  check(press(&menu, MENU_B) == MENU_STAY && menu.page == MENU_PAGE_OPTIONS &&
            menu.option_row == MENU_GROUP_FRONTEND,
        "B goes back to Options on the group");
  check(menu_group_size(&menu, MENU_GROUP_SPLORE) == 4 &&
            menu_group_setting(&menu, MENU_GROUP_SPLORE, 0) == MENU_COPY_CARTS &&
            menu_group_setting(&menu, MENU_GROUP_SPLORE, 1) == MENU_SPLORE_ARTWORK &&
            menu_group_setting(&menu, MENU_GROUP_SPLORE, 2) == MENU_HIDE_MATURE &&
            menu_group_setting(&menu, MENU_GROUP_SPLORE, 3) == MENU_CHECK_UPDATES,
        "Splore holds every Splore setting and nothing else");
  select_setting(&menu, MENU_SCREEN_MODE);

  menu_option_value(&menu, MENU_SCREEN_MODE, value, sizeof value);
  check(strcmp(value, "Standard") == 0, "Screen Mode shows Standard");
  press(&menu, MENU_RIGHT);
  check(settings.screen_mode == MENU_SCREEN_MODE_STRETCHED &&
            file_is(screen_mode_path, "stretched\n"),
        "Right switches to stretched and saves it");
  press(&menu, MENU_RIGHT);
  check(settings.screen_mode == 0 && file_is(screen_mode_path, "standard\n"),
        "Right wraps back to standard");
  press(&menu, MENU_A);
  check(settings.screen_mode == MENU_SCREEN_MODE_STRETCHED, "A steps a value too");
  check(menu_read_screen_mode(screen_mode_path) == MENU_SCREEN_MODE_STRETCHED,
        "the saved screen mode reads back");
  write_file(screen_mode_path, "sideways\n");
  check(menu_read_screen_mode(screen_mode_path) == 0, "an unknown screen mode reads as standard");

  select_setting(&menu, MENU_OVERLAY);
  menu_option_value(&menu, MENU_OVERLAY, value, sizeof value);
  check(strcmp(value, "None") == 0, "Overlay shows None with no overlay chosen");
  press(&menu, MENU_RIGHT);
  menu_option_value(&menu, MENU_OVERLAY, value, sizeof value);
  check(settings.overlay == 0 && file_is(overlay_path, "a.png\n") && strcmp(value, "a") == 0,
        "Right picks the first PNG, shown without .png");
  press(&menu, MENU_LEFT);
  press(&menu, MENU_LEFT);
  check(settings.overlay == 1 && file_is(overlay_path, "b.png\n"),
        "Left from None wraps to the last PNG");
  press(&menu, MENU_RIGHT);
  check(settings.overlay == -1 && file_is(overlay_path, "disabled\n"),
        "None is saved as disabled");
  check(menu_read_overlay(overlay_path, overlay_names, 2) == -1, "disabled reads back as none");
  write_file(overlay_path, "b.png\r\n");
  check(menu_read_overlay(overlay_path, overlay_names, 2) == 1, "a saved overlay reads back");
}

static void test_copy_carts(void)
{
  struct menu_settings settings;
  struct menu menu;
  char path[8192];

  settings_for_test(&settings);
  snprintf(path, sizeof path, "%s/copy-carts-favorites", shared);
  write_file(path, "");
  snprintf(path, sizeof path, "%s/copy-carts", userdata);
  write_file(path, "");

  menu = open_options(&settings);
  check(menu.copy_carts == MENU_COPY_FAVOURITES,
        "a favourites marker in the shared folder, spelt either way, wins");

  select_setting(&menu, MENU_COPY_CARTS);
  press(&menu, MENU_RIGHT);
  check(menu.copy_carts == MENU_COPY_OFF && !exists(userdata, "copy-carts") &&
            !exists(shared, "copy-carts-favorites"),
        "Off removes the markers from both folders");
  press(&menu, MENU_RIGHT);
  check(exists(userdata, "copy-carts") && !exists(shared, "copy-carts"),
        "All writes copy-carts to the userdata folder");
  press(&menu, MENU_RIGHT);
  check(exists(userdata, "copy-carts-favourites") && !exists(userdata, "copy-carts"),
        "Favourites replaces it with copy-carts-favourites");

  menu_open(&menu, &settings, "Cart");
  check(menu.copy_carts == MENU_COPY_FAVOURITES, "the marker written reads back");
  snprintf(path, sizeof path, "%s/copy-carts-favourites", userdata);
  unlink(path);
  snprintf(path, sizeof path, "%s/copy-carts", shared);
  write_file(path, "");
  menu_open(&menu, &settings, "Cart");
  check(menu.copy_carts == MENU_COPY_ALL, "copy-carts in the shared folder reads as All");
  unlink(path);
}

static void test_splore_artwork(void)
{
  struct menu_settings settings;
  struct menu menu;
  char value[64];

  settings_for_test(&settings);
  menu = open_options(&settings);
  menu_option_value(&menu, MENU_SPLORE_ARTWORK, value, sizeof value);
  check(strcmp(value, "Standard") == 0, "Splore Artwork starts on Standard with no file");

  select_setting(&menu, MENU_SPLORE_ARTWORK);
  press(&menu, MENU_RIGHT);
  check(file_is(splore_artwork_path, "fancy\n"), "Right picks the fancy cover and saves it");
  check(menu_read_splore_artwork(splore_artwork_path) == 1, "the saved cover reads back");
  menu_open(&menu, &settings, "Cart");
  menu_option_value(&menu, MENU_SPLORE_ARTWORK, value, sizeof value);
  check(strcmp(value, "Fancy") == 0, "the menu opens on the saved cover");

  press(&menu, MENU_DOWN);
  press(&menu, MENU_A);
  select_setting(&menu, MENU_SPLORE_ARTWORK);
  press(&menu, MENU_RIGHT);
  check(file_is(splore_artwork_path, "standard\n"), "Right wraps back to the standard cover");

  write_file(splore_artwork_path, "shiny\n");
  check(menu_read_splore_artwork(splore_artwork_path) == 0, "an unknown cover reads as standard");
  unlink(splore_artwork_path);
}

/*
 * A description's note on when it applies sits on the line under it, which is
 * checked by drawing the PICO-8 rows' descriptions and finding ink on two lines.
 */
static void test_description_lines(void)
{
  struct menu_settings settings;
  struct menu menu;
  gfx_canvas canvas = guarded_canvas(1280, 720);
  gfx_metrics m = gfx_metrics_for(1280, 720);
  gfx_font *font = gfx_font_open(NULL);
  gfx_theme theme;
  int leading = gfx_u(&m, 12), desc_top = 720 - gfx_u(&m, m.padding) - 2 * leading;
  int line, x, y, inked[2] = {0, 0};

  settings_for_test(&settings);
  gfx_theme_init(&theme, "minui", NULL);
  menu = open_options(&settings);
  select_setting(&menu, MENU_SHOW_FPS);
  menu_draw(&menu, &canvas, NULL, font, &theme, NULL);

  for (line = 0; line < 2; line++) {
    for (y = desc_top + line * leading; y < desc_top + (line + 1) * leading; y++) {
      for (x = 0; x < 1280; x++) {
        inked[line] |= canvas.pixels[(size_t)y * 1280 + x] == gfx_hint_text(&theme);
      }
    }
  }
  check(inked[0] && inked[1], "a setting's note on when it applies is on a line of its own");

  gfx_font_close(font);
  free(canvas.pixels - GUARD);
}

static void test_config(void)
{
  struct menu_settings settings;
  struct menu menu;
  char value[64];

  settings_for_test(&settings);
  write_file(pico8_config_path, "show_fps 1\nsplore_filter 1\n");
  write_file(installed_config_path,
             "show_fps 0 // Draw frames per second\nvolume 128\nforeground_sleep_ms 7\n");

  menu = open_options(&settings);
  menu_option_value(&menu, MENU_SHOW_FPS, value, sizeof value);
  check(strcmp(value, "On") == 0, "a setting the menu saved wins over config.txt");
  menu_option_value(&menu, MENU_VOLUME, value, sizeof value);
  check(strcmp(value, "50%") == 0, "a setting never changed is read from config.txt");
  menu_option_value(&menu, MENU_BATTERY_SAVER, value, sizeof value);
  check(strcmp(value, "Off") == 0, "a value the menu does not offer shows the default");
  menu_option_value(&menu, MENU_CHECK_UPDATES, value, sizeof value);
  check(strcmp(value, "On") == 0, "a setting in neither file shows the default");

  select_setting(&menu, MENU_VOLUME);
  press(&menu, MENU_RIGHT);
  check(file_is(pico8_config_path, "show_fps 1\nsplore_filter 1\nvolume 192\n"),
        "changing a setting adds it and keeps the others");
  select_setting(&menu, MENU_SHOW_FPS);
  press(&menu, MENU_LEFT);
  check(file_is(pico8_config_path, "show_fps 0\nsplore_filter 1\nvolume 192\n"),
        "changing it again replaces its line");
  select_setting(&menu, MENU_BATTERY_SAVER);
  press(&menu, MENU_RIGHT);
  check(file_is(pico8_config_path, "show_fps 0\nsplore_filter 1\nvolume 192\nforeground_sleep_ms 10\n"),
        "Battery Saver writes a longer sleep");

  unlink(pico8_config_path);
  unlink(installed_config_path);
}

static gfx_canvas guarded_canvas(int w, int h)
{
  size_t i, count = (size_t)w * h + 2 * GUARD;
  uint32_t *pixels = malloc(count * sizeof *pixels);
  gfx_canvas canvas = {NULL, w, h};

  if (pixels == NULL) {
    perror("malloc");
    exit(2);
  }
  for (i = 0; i < count; i++) {
    pixels[i] = GUARD_VALUE;
  }
  canvas.pixels = pixels + GUARD;
  return canvas;
}

static int guards_intact(const gfx_canvas *canvas)
{
  size_t i, count = (size_t)canvas->w * canvas->h;

  for (i = 0; i < GUARD; i++) {
    if (canvas->pixels[-(long)GUARD + (long)i] != GUARD_VALUE ||
        canvas->pixels[count + i] != GUARD_VALUE) {
      return 0;
    }
  }
  return 1;
}

static void test_drawing(const char *minui_font, const char *nextui_font)
{
  static const char *const styles[] = {"minui", "nextui"};
  const char *fonts[] = {minui_font, nextui_font, NULL};
  struct menu_settings settings;
  gfx_battery batteries[] = {{-1, 0}, {5, 0}, {50, 1}};
  char name[256];
  size_t s, t, f;
  int all_inside = 1, selected_drawn = 1, dimmed = 1, cleared = 1;

  settings_for_test(&settings);
  for (s = 0; s < sizeof sizes / sizeof *sizes; s++) {
    int w = sizes[s][0], h = sizes[s][1];
    gfx_metrics m = gfx_metrics_for(w, h);
    gfx_canvas canvas = guarded_canvas(w, h);
    uint32_t *frame = malloc((size_t)w * h * sizeof *frame);
    size_t i;

    for (i = 0; i < (size_t)w * h; i++) {
      frame[i] = GFX_ARGB(0xc0c0c0);
    }

    for (t = 0; t < 2; t++) {
      for (f = 0; f < 3; f++) {
        gfx_font *font = gfx_font_open(fonts[f]);
        gfx_theme theme;
        struct menu menu;
        int pill = gfx_u(&m, GFX_PILL_SIZE);
        int top = (h - gfx_u(&m, m.padding * 2) - MENU_ROOT_COUNT * pill) / 2 + gfx_u(&m, m.padding);
        int group, row, rows;

        gfx_theme_init(&theme, styles[t], NULL);
        menu_open(&menu, &settings,
                  "A cart whose name is far too long to fit in the title at all, ever");
        /* each font also draws the battery's place a different way */
        menu.indicator = f == 0 ? MENU_INDICATOR_NONE
                         : f == 1 ? MENU_INDICATOR_VOLUME
                                  : MENU_INDICATOR_BRIGHTNESS;
        menu.indicator_value = 7;
        menu_draw(&menu, &canvas, frame, font, &theme, &batteries[f]);
        all_inside &= guards_intact(&canvas);
        selected_drawn &= canvas.pixels[(size_t)(top + pill / 2) * w + gfx_u(&m, m.padding) + 2] ==
                          gfx_selected_pill(&theme);
        dimmed &= canvas.pixels[(size_t)(h / 2) * w + w - 1] == GFX_ARGB(0x5f5f5f);

        /* Options, then every row of every group's page */
        press(&menu, MENU_DOWN);
        press(&menu, MENU_A);
        for (group = 0; group < MENU_GROUP_COUNT; group++) {
          menu_draw(&menu, &canvas, frame, font, &theme, &batteries[f]);
          all_inside &= guards_intact(&canvas);
          cleared &= canvas.pixels[(size_t)(h - 1) * w] == gfx_background(&theme);
          press(&menu, MENU_A);
          rows = group == MENU_GROUP_SHORTCUTS  ? MENU_SHORTCUT_COUNT
                 : group == MENU_GROUP_CONTROLS ? MENU_CONTROL_ROWS
                                                : menu_group_size(&menu, group);
          for (row = 0; row < rows; row++) {
            menu_draw(&menu, &canvas, frame, font, &theme, &batteries[f]);
            all_inside &= guards_intact(&canvas);
            cleared &= canvas.pixels[(size_t)(h - 1) * w] == gfx_background(&theme);
            press(&menu, MENU_DOWN);
          }
          press(&menu, MENU_B);
          press(&menu, MENU_DOWN);
        }
        gfx_font_close(font);
      }
    }

    snprintf(name, sizeof name, "%dx%d: every page stays inside the canvas", w, h);
    check(all_inside, name);
    free(frame);
    free(canvas.pixels - GUARD);
  }

  check(selected_drawn, "the selected root row is a pill in the theme's colour");
  check(dimmed, "the game shows through the root page dimmed by half");
  check(cleared, "the Options page clears the game away");
}

/* finds a pixel of colour in the box, for icons drawn without a fixed shape */
static int has_colour(const gfx_canvas *canvas, int x, int y, int w, int h, uint32_t colour)
{
  int row, col;

  for (row = y; row < y + h; row++) {
    for (col = x; col < x + w; col++) {
      if (canvas->pixels[(size_t)row * canvas->w + col] == colour) {
        return 1;
      }
    }
  }
  return 0;
}

static void test_indicator_drawing(void)
{
  gfx_canvas canvas = guarded_canvas(372, 90);
  gfx_theme theme;
  int w, h;
  /* the Brick's scale: the bar starts a pill in, 39 pixels down, 240 by 12 */
  int bar_x = 90, bar_y = 39, bar_w = 240;

  menu_indicator_size(1024, 768, &w, &h);
  check(w == 372 && h == 90, "the indicator is minarch's size, 124 by 30 units");
  menu_indicator_size(640, 480, &w, &h);
  check(w == 248 && h == 60, "and scales with the screen");

  gfx_theme_init(&theme, "minui", NULL);
  gfx_clear(&canvas, 0);
  menu_draw_indicator(&canvas, 1024, 768, &theme, 0, 0, MENU_INDICATOR_VOLUME, 10);
  check(guards_intact(&canvas), "the indicator stays inside its canvas");
  check(canvas.pixels[0] == 0, "its rounded ends leave the corners clear for the game");
  check(canvas.pixels[(size_t)(bar_y + 6) * 372 + bar_x + bar_w / 4] == gfx_indicator_bar(&theme) &&
            canvas.pixels[(size_t)(bar_y + 6) * 372 + bar_x + bar_w * 3 / 4] ==
                gfx_indicator_track(&theme),
        "volume 10 of 20 fills half the bar");
  check(has_colour(&canvas, 0, 0, 90, 90, gfx_indicator_icon(&theme)), "the speaker is drawn");

  gfx_clear(&canvas, 0);
  menu_draw_indicator(&canvas, 1024, 768, &theme, 0, 0, MENU_INDICATOR_VOLUME, 0);
  check(canvas.pixels[(size_t)(bar_y + 6) * 372 + bar_x + 12] == gfx_indicator_track(&theme),
        "volume 0 leaves the bar empty");

  gfx_clear(&canvas, 0);
  menu_draw_indicator(&canvas, 1024, 768, &theme, 0, 0, MENU_INDICATOR_BRIGHTNESS, 10);
  check(canvas.pixels[(size_t)(bar_y + 6) * 372 + bar_x + bar_w - 12] == gfx_indicator_bar(&theme),
        "brightness 10 of 10 fills the whole bar");

  gfx_clear(&canvas, 0);
  menu_draw_indicator(&canvas, 1024, 768, &theme, 0, 0, MENU_INDICATOR_VOLUME, 99);
  check(guards_intact(&canvas), "a value past the end is drawn as the end");

  free(canvas.pixels - GUARD);
}

/*
 * Every page fits on a 480 line screen, so the list scrolls on a canvas too
 * short for the Shortcuts page, which at the smallest scale fits five rows
 */
static void test_scrolling(void)
{
  struct menu_settings settings;
  struct menu menu;
  gfx_canvas canvas = guarded_canvas(640, 100);
  gfx_font *font = gfx_font_open(NULL);
  gfx_theme theme;

  settings_for_test(&settings);
  gfx_theme_init(&theme, "minui", NULL);
  menu = open_options(&settings);
  press(&menu, MENU_UP);
  press(&menu, MENU_A);
  press(&menu, MENU_UP);
  menu_draw(&menu, &canvas, NULL, font, &theme, NULL);
  check(menu.shortcut_row == MENU_SHORTCUT_COUNT - 1 && menu.shortcut_top > 0,
        "the last row scrolls a list too long for the screen");
  press(&menu, MENU_DOWN);
  menu_draw(&menu, &canvas, NULL, font, &theme, NULL);
  check(menu.shortcut_row == 0 && menu.shortcut_top == 0, "Down from the last row wraps to the top");
  check(!menu.dirty, "drawing leaves the menu clean");
  gfx_font_close(font);
  free(canvas.pixels - GUARD);
}

static void test_fonts(const char *minui_font, const char *nextui_font)
{
  char path[8192], fitted[64];
  gfx_font *font;
  const char *fonts[] = {minui_font, nextui_font};
  size_t i;

  font = gfx_font_open(NULL);
  check(font != NULL && gfx_font_is_builtin(font), "no font path uses the built-in font");
  check(gfx_text_width(font, 32, "A") == 20 && gfx_text_width(font, 32, "AB") == 44,
        "the built-in font is five pixels and a gap per letter, scaled");
  gfx_font_close(font);

  snprintf(path, sizeof path, "%s/missing.ttf", dir);
  font = gfx_font_open(path);
  check(gfx_font_is_builtin(font), "a missing font falls back to the built-in one");
  gfx_font_close(font);

  snprintf(path, sizeof path, "%s/not-a-font.ttf", dir);
  write_file(path, "this is not a font");
  font = gfx_font_open(path);
  check(gfx_font_is_builtin(font), "a file that is not a font falls back to the built-in one");
  gfx_font_close(font);

  for (i = 0; i < 2; i++) {
    font = gfx_font_open(fonts[i]);
    snprintf(path, sizeof path, "%s loads", i == 0 ? "MinUI's CFF font" : "NextUI's TrueType font");
    check(!gfx_font_is_builtin(font), path);
    snprintf(path, sizeof path, "%s measures text", i == 0 ? "MinUI's font" : "NextUI's font");
    check(gfx_text_width(font, 32, "Continue") > gfx_text_width(font, 32, "Quit") &&
              gfx_line_height(font, 32) >= 32 && gfx_line_height(font, 32) <= 64,
          path);

    gfx_text_fit(font, 32, "Continue", 1000, fitted, sizeof fitted);
    check(strcmp(fitted, "Continue") == 0, "text that fits is left alone");
    gfx_text_fit(font, 32, "Celeste Classic 2: Lani's Trek", 200, fitted, sizeof fitted);
    check(gfx_text_width(font, 32, fitted) <= 200 && strstr(fitted, "...") != NULL,
          "text that does not fit is cut with an ellipsis");
    gfx_text_fit(font, 32, "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9", 80, fitted,
                 sizeof fitted);
    check(strlen(fitted) >= 3 && (strlen(fitted) - 3) % 2 == 0,
          "cutting text never splits a character");
    gfx_font_close(font);
  }
}

static void test_theme(void)
{
  gfx_theme theme;
  uint32_t colour;

  gfx_theme_init(&theme, "minui", "0x112233FF");
  check(!theme.nextui && gfx_selected_pill(&theme) == GFX_WHITE &&
            gfx_title_pill(&theme) == GFX_BLACK && gfx_shadow(&theme),
        "MinUI's look is fixed, whatever colours are given");

  gfx_theme_init(&theme, "nextui", "0x112233FF,,bogus,#445566");
  check(theme.nextui && theme.color[0] == GFX_ARGB(0x112233) &&
            theme.color[1] == GFX_ARGB(0x9b2257) && theme.color[2] == GFX_ARGB(0x1e2329) &&
            theme.color[3] == GFX_ARGB(0x445566) && !gfx_shadow(&theme),
        "NextUI's colours are read in order, keeping the default for empty or bad ones");
  check(gfx_selected_pill(&theme) == theme.color[0] && gfx_title_pill(&theme) == theme.color[1] &&
            gfx_background(&theme) == theme.color[6],
        "NextUI's colours take their roles");

  check(gfx_parse_colour("0xAABBCC", &colour) && colour == GFX_ARGB(0xaabbcc),
        "a colour may leave out its alpha");
  check(!gfx_parse_colour("0xAABBC", &colour) && !gfx_parse_colour("0xGGBBCCDD", &colour),
        "a colour of the wrong length or with other letters is refused");
}

static void test_battery(void)
{
  char root[4096], path[8192];
  gfx_battery battery;

  snprintf(root, sizeof root, "%s/power", dir);
  make_dir(root);
  battery = gfx_read_battery(root);
  check(battery.percent == -1, "no supply means no battery");

  snprintf(path, sizeof path, "%s/ac", root);
  make_dir(path);
  snprintf(path, sizeof path, "%s/ac/type", root);
  write_file(path, "Mains\n");
  snprintf(path, sizeof path, "%s/ac/capacity", root);
  write_file(path, "77\n");
  check(gfx_read_battery(root).percent == -1, "a supply that is not a battery is passed over");

  snprintf(path, sizeof path, "%s/battery", root);
  make_dir(path);
  snprintf(path, sizeof path, "%s/battery/type", root);
  write_file(path, "Battery\n");
  snprintf(path, sizeof path, "%s/battery/capacity", root);
  write_file(path, "150\n");
  snprintf(path, sizeof path, "%s/battery/status", root);
  write_file(path, "Charging\n");
  battery = gfx_read_battery(root);
  check(battery.percent == 100 && battery.charging,
        "the battery's capacity is read, kept to 100, with its charging state");
}

static void make_dirs(const char *path)
{
  char partial[8192];
  size_t i;

  snprintf(partial, sizeof partial, "%s", path);
  for (i = 1; partial[i] != '\0'; i++) {
    if (partial[i] == '/') {
      partial[i] = '\0';
      mkdir(partial, 0755);
      partial[i] = '/';
    }
  }
  mkdir(partial, 0755);
}

static void put_file(const char *folder, const char *name, const char *text)
{
  char path[16384];

  make_dirs(folder);
  snprintf(path, sizeof path, "%s/%s", folder, name);
  write_file(path, text);
}

static int same_text(const char *path, const char *expected)
{
  return file_is(path, expected);
}

static void test_cart_title(void)
{
  char roms[8192], cart[16384], title[256];

  snprintf(roms, sizeof roms, "%s/titles/Pico-8 (PICO)", dir);
  make_dirs(roms);

  snprintf(cart, sizeof cart, "%s/Celeste.p8.png", roms);
  cart_title(cart, title, sizeof title);
  check(strcmp(title, "Celeste") == 0, "a cart's title drops .p8.png");
  snprintf(cart, sizeof cart, "%s/Celeste.P8", roms);
  cart_title(cart, title, sizeof title);
  check(strcmp(title, "Celeste") == 0, "a cart's title drops .p8 whatever its case");
  snprintf(cart, sizeof cart, "%s/Celeste.png", roms);
  cart_title(cart, title, sizeof title);
  check(strcmp(title, "Celeste") == 0, "a cart's title drops .png");

  snprintf(cart, sizeof cart, "%s/1) Splore.p8", roms);
  cart_title(cart, title, sizeof title);
  check(strcmp(title, "Splore") == 0, "a cart's title drops the number it is sorted by");
  snprintf(cart, sizeof cart, "%s/A) Celeste.p8", roms);
  cart_title(cart, title, sizeof title);
  check(strcmp(title, "A) Celeste") == 0, "but only a number");
  snprintf(cart, sizeof cart, "%s/2048.p8", roms);
  cart_title(cart, title, sizeof title);
  check(strcmp(title, "2048") == 0, "a title that is a number is kept");

  put_file(roms, "map.txt", "other.p8.png\tOther\r\n15133.p8.png\tCeleste Classic 2\r\n");
  snprintf(cart, sizeof cart, "%s/15133.p8.png", roms);
  cart_title(cart, title, sizeof title);
  check(strcmp(title, "Celeste Classic 2") == 0, "a cart's row in map.txt names it");
  snprintf(cart, sizeof cart, "%s/unlisted.p8", roms);
  cart_title(cart, title, sizeof title);
  check(strcmp(title, "unlisted") == 0, "a cart map.txt does not list is named by its file");

  snprintf(cart, sizeof cart, "%s/Poom", roms);
  put_file(cart, "Poom.m3u", "poom_0.p8\n");
  snprintf(cart, sizeof cart, "%s/Poom/poom_0.p8", roms);
  cart_title(cart, title, sizeof title);
  check(strcmp(title, "Poom") == 0, "a multi-cart game is named after its folder");
  put_file(roms, "map.txt", "Poom\tPOOM 1.9\n");
  cart_title(cart, title, sizeof title);
  check(strcmp(title, "POOM 1.9") == 0, "or its folder's row in map.txt");
}

static void test_switcher_preview(void)
{
  char roms[8192], switcher[8192], cart[16384], path[16384], artwork[16384];
  uint32_t frame[2 * 3];
  unsigned char bmp[256];
  FILE *file;
  size_t got;
  int i;

  snprintf(roms, sizeof roms, "%s/preview/Pico-8 (PICO)", dir);
  snprintf(switcher, sizeof switcher, "%s/preview/.minui/PICO", dir);
  make_dirs(roms);

  put_file(roms, "Text.p8", "pico-8 cartridge // http://www.pico-8.com\n");
  snprintf(cart, sizeof cart, "%s/Text.p8", roms);
  check(!cart_artwork(cart, artwork, sizeof artwork) &&
            !cart_write_preview_artwork(switcher, cart),
        "a text cart with no artwork gives the switcher no preview");
  snprintf(path, sizeof path, "%s/Text.p8.txt", switcher);
  check(access(path, F_OK) != 0, "and no slot, as NextUI would show one it cannot load");

  put_file(roms, "Label.p8", "\x89PNG\r\n\x1a\nlabel");
  snprintf(cart, sizeof cart, "%s/Label.p8", roms);
  check(cart_write_preview_artwork(switcher, cart), "a .p8 cart that is a PNG is its own preview");
  snprintf(path, sizeof path, "%s/Label.p8.0.bmp", switcher);
  check(same_text(path, "\x89PNG\r\n\x1a\nlabel"), "copied under the name the switcher reads");
  snprintf(path, sizeof path, "%s/Label.p8.txt", switcher);
  check(same_text(path, "0\n"), "with slot 0 named for it");

  snprintf(path, sizeof path, "%s/.media", roms);
  put_file(path, "Label.png", "media artwork");
  check(cart_artwork(cart, artwork, sizeof artwork) && strstr(artwork, "/.media/Label.png") != NULL,
        "artwork in .media is preferred to the cart");

  snprintf(path, sizeof path, "%s/Poom", roms);
  put_file(path, "Poom.m3u", "poom_0.p8\n");
  put_file(path, "poom_0.p8", "\x89PNG\r\n\x1a\npoom");
  snprintf(path, sizeof path, "%s/.media", roms);
  put_file(path, "Poom.png", "folder artwork");
  snprintf(cart, sizeof cart, "%s/Poom/poom_0.p8", roms);
  check(cart_write_preview_artwork(switcher, cart),
        "a multi-cart game gets the preview of its folder");
  snprintf(path, sizeof path, "%s/Poom.m3u.0.bmp", switcher);
  check(same_text(path, "folder artwork"), "keyed by its m3u, with the folder's artwork");

  for (i = 0; i < 6; i++) {
    frame[i] = 0xff000000u | (uint32_t)(i * 0x101010);
  }
  snprintf(cart, sizeof cart, "%s/Label.p8", roms);
  check(cart_write_preview_frame(switcher, cart, frame, 2, 3, 0), "a frame becomes the preview");
  snprintf(path, sizeof path, "%s/Label.p8.0.bmp", switcher);
  file = fopen(path, "rb");
  got = file != NULL ? fread(bmp, 1, sizeof bmp, file) : 0;
  if (file != NULL) {
    fclose(file);
  }
  /* 2x3 at 3 bytes a pixel is 6 bytes a row, padded to 8, bottom row first */
  check(got == 54 + 8 * 3 && bmp[0] == 'B' && bmp[1] == 'M' && bmp[18] == 2 && bmp[22] == 3 &&
            bmp[28] == 24,
        "written as a 24-bit BMP of the frame's size");
  check(bmp[54] == 0x40 && bmp[54 + 3] == 0x50 && bmp[54 + 16] == 0x00 && bmp[54 + 16 + 3] == 0x10,
        "with the bottom row first and every pixel blue, green, red");

  check(!cart_write_preview_artwork(NULL, cart) && !cart_write_preview_artwork("", cart),
        "nothing is written without a switcher folder, as under MinUI");
}

static void test_preview_size(void)
{
  char roms[8192], switcher[8192], cart[16384], path[16384];
  static uint32_t brick[1024 * 768];
  unsigned char header[57];
  struct stat info;
  FILE *file;
  int x, y, ok;

  check(cart_preview_step(1024, 768) == 3,
        "the Brick's preview is a third of its screen, pico-8's pixels two wide");
  check(cart_preview_step(1280, 720) == 2 && cart_preview_step(720, 720) == 2,
        "720 line screens give a half");
  check(cart_preview_step(640, 480) == 1 && cart_preview_step(2, 3) == 1,
        "a screen under 512 lines is kept whole");

  /* each pixel names its own row and column, so sampling can be read back */
  for (y = 0; y < 768; y++) {
    for (x = 0; x < 1024; x++) {
      brick[(size_t)y * 1024 + x] = 0xff000000u | (uint32_t)(y & 0xff) << 8 | (uint32_t)(x & 0xff);
    }
  }

  snprintf(roms, sizeof roms, "%s/brick/Pico-8 (PICO)", dir);
  snprintf(switcher, sizeof switcher, "%s/brick/.minui/PICO", dir);
  put_file(roms, "Celeste.p8", "cart");
  snprintf(cart, sizeof cart, "%s/Celeste.p8", roms);
  check(cart_write_preview_frame(switcher, cart, brick, 1024, 768, 0), "a Brick frame is written");

  snprintf(path, sizeof path, "%s/Celeste.p8.0.bmp", switcher);
  file = fopen(path, "rb");
  ok = file != NULL && fread(header, 1, sizeof header, file) == sizeof header;
  if (file != NULL) {
    fclose(file);
  }
  /* 341 pixels of 3 bytes is 1023, padded to 1024 a row */
  check(ok && stat(path, &info) == 0 && info.st_size == 54 + 1024 * 256 &&
            (header[18] | header[19] << 8) == 341 && (header[22] | header[23] << 8) == 256,
        "at 341x256, about 260 KB rather than 2.3 MB");
  /* the bottom row comes first, and is the screen's row 765, 255 times 3 */
  check(ok && header[54] == 0 && header[55] == (765 & 0xff), "every third row and column is kept");

  snprintf(path, sizeof path, "%s/Celeste.p8.0.bmp.part", switcher);
  check(access(path, F_OK) != 0, "nothing is left under the temporary name");
}

static void test_shortcuts_page(void)
{
  char path[8192], name[64];
  struct menu_settings settings;
  struct menu menu;
  struct menu_binding read_back[MENU_SHORTCUT_COUNT];

  snprintf(path, sizeof path, "%s/shortcuts", dir);
  settings_for_test(&settings);
  settings.shortcuts_path = path;
  menu_read_shortcuts(path, settings.shortcuts);
  check(settings.shortcuts[MENU_SHORTCUT_SCREENSHOT].button == -1,
        "every shortcut starts unbound, as NextUI's do");
  menu_binding_name(&settings.shortcuts[MENU_SHORTCUT_SCREENSHOT], name, sizeof name);
  check(strcmp(name, "None") == 0, "and shows None");

  menu = open_options(&settings);
  press(&menu, MENU_UP);
  check(menu.option_row == MENU_GROUP_SHORTCUTS, "Shortcuts is the last Options group");
  press(&menu, MENU_A);
  check(menu.page == MENU_PAGE_SHORTCUTS && menu.shortcut_row == 0, "A on it opens the Shortcuts page");

  press(&menu, MENU_A);
  check(menu_is_binding(&menu), "A on a shortcut waits for its button");
  check(press(&menu, MENU_B) == MENU_STAY && menu_is_binding(&menu),
        "while waiting the menu's own buttons do nothing, as the next press is the binding");
  menu_bind(&menu, 2, 1);
  menu_binding_name(&settings.shortcuts[MENU_SHORTCUT_SCREENSHOT], name, sizeof name);
  check(!menu_is_binding(&menu) && strcmp(name, "MENU+X") == 0, "Menu held with X binds MENU+X");
  check(file_is(path, "screenshot menu+x\n"), "and saves it");

  press(&menu, MENU_DOWN);
  check(strcmp(menu_shortcut_label(menu.shortcut_row), "Save GIF") == 0,
        "Save GIF follows Screenshot");
  press(&menu, MENU_DOWN);
  press(&menu, MENU_A);
  menu_bind(&menu, 2, 1);
  menu_binding_name(&settings.shortcuts[MENU_SHORTCUT_SCREENSHOT], name, sizeof name);
  check(strcmp(name, "None") == 0 && settings.shortcuts[MENU_SHORTCUT_QUIT].button == 2,
        "binding the same button to another shortcut takes it from the first");
  menu_bind(&menu, 9, 0);
  check(settings.shortcuts[MENU_SHORTCUT_QUIT].button == 2, "nothing is bound once the wait is over");

  press(&menu, MENU_A);
  menu_bind(&menu, 5, 0);
  check(menu_is_binding(&menu), "Menu itself cannot be bound");
  check(press(&menu, MENU_MENU) == MENU_STAY && !menu_is_binding(&menu),
        "a tap of Menu gives up waiting rather than closing the menu");

  press(&menu, MENU_DOWN);
  press(&menu, MENU_A);
  menu_bind(&menu, 9, 0);
  menu_binding_name(&settings.shortcuts[MENU_SHORTCUT_RESET], name, sizeof name);
  check(strcmp(name, "L1") == 0, "a button pressed alone binds it alone");
  check(file_is(path, "quit menu+x\nreset l1\n"), "and every binding is saved");

  press(&menu, MENU_X);
  check(settings.shortcuts[MENU_SHORTCUT_RESET].button == -1 && file_is(path, "quit menu+x\n"),
        "X clears the shortcut");

  menu_read_shortcuts(path, read_back);
  check(read_back[MENU_SHORTCUT_QUIT].button == 2 && read_back[MENU_SHORTCUT_QUIT].with_menu &&
            read_back[MENU_SHORTCUT_RESET].button == -1,
        "the saved shortcuts read back");
  write_file(path, "quit menu+menu\nreset sideways\nnonsense a\n");
  menu_read_shortcuts(path, read_back);
  check(read_back[MENU_SHORTCUT_QUIT].button == -1 && read_back[MENU_SHORTCUT_RESET].button == -1,
        "a binding to Menu, or to no button, reads as unbound");

  write_file(path, "save-gif menu+a\ncpu-meter r1\n");
  menu_read_shortcuts(path, read_back);
  check(read_back[MENU_SHORTCUT_SAVE_GIF].button == 0 &&
            read_back[MENU_SHORTCUT_SAVE_GIF].with_menu &&
            read_back[MENU_SHORTCUT_CPU_METER].button == 10 &&
            strcmp(menu_shortcut_label(MENU_SHORTCUT_CPU_METER), "Toggle CPU Meter") == 0,
        "Save GIF and Toggle CPU Meter read back by their names");

  /* L2 and R2 are the triggers, numbered after SDL's buttons, and none in between can be bound */
  write_file(path, "quit l2\nreset menu+r2\n");
  menu_read_shortcuts(path, read_back);
  check(read_back[MENU_SHORTCUT_QUIT].button == MENU_BUTTON_L2 &&
            !read_back[MENU_SHORTCUT_QUIT].with_menu &&
            read_back[MENU_SHORTCUT_RESET].button == MENU_BUTTON_R2 &&
            read_back[MENU_SHORTCUT_RESET].with_menu,
        "L2 and R2 read back as the triggers");
  menu_binding_name(&read_back[MENU_SHORTCUT_RESET], name, sizeof name);
  check(strcmp(name, "MENU+R2") == 0, "and show by their names");
  check(!menu_is_bindable(15) && !menu_is_bindable(20) && menu_is_bindable(MENU_BUTTON_L2),
        "SDL's buttons after Right cannot be bound");
  press(&menu, MENU_DOWN);
  press(&menu, MENU_A);
  menu_bind(&menu, MENU_BUTTON_L2, 0);
  check(file_is(path, "quit menu+x\nscreen-mode l2\n"), "and L2 saves as l2");
  press(&menu, MENU_X);
  press(&menu, MENU_UP);

  check(press(&menu, MENU_B) == MENU_STAY && menu.page == MENU_PAGE_OPTIONS &&
            menu.option_row == MENU_GROUP_SHORTCUTS,
        "B goes back to Options on the Shortcuts row");
  unlink(path);
}

/* opens the Controls page from the root page */
static struct menu open_controls(struct menu_settings *settings)
{
  struct menu menu = open_options(settings);

  while (menu.option_row != MENU_GROUP_CONTROLS) {
    press(&menu, MENU_DOWN);
  }
  press(&menu, MENU_A);
  return menu;
}

static void test_controls_page(void)
{
  char path[8192], shortcuts_path[8192], name[64];
  struct menu_settings settings;
  struct menu menu;
  struct menu_binding read_back[MENU_CONTROL_COUNT];
  const char *labels[] = {"Up", "Down", "Left", "Right", "O", "X", "Pause"};
  const char *buttons[] = {"UP", "DOWN", "LEFT", "RIGHT", "A", "B", "START"};
  int i, defaults_shown = 1, labelled = 1;

  snprintf(path, sizeof path, "%s/controls", dir);
  snprintf(shortcuts_path, sizeof shortcuts_path, "%s/shortcuts", dir);
  settings_for_test(&settings);
  settings.controls_path = path;
  settings.shortcuts_path = shortcuts_path;
  menu_read_shortcuts(shortcuts_path, settings.shortcuts);
  menu_read_controls(path, settings.controls);
  for (i = 0; i < MENU_CONTROL_COUNT; i++) {
    menu_binding_name(&settings.controls[i], name, sizeof name);
    defaults_shown &= strcmp(name, buttons[i]) == 0;
    labelled &= strcmp(menu_control_label(i), labels[i]) == 0;
  }
  check(defaults_shown, "with no file every control has the button PICO-8 reads it from");
  check(labelled, "the controls are named for PICO-8's buttons, in minarch's order");
  check(menu_control_button(MENU_CONTROL_O) == 0 && menu_control_button(MENU_CONTROL_X) == 1 &&
            menu_control_button(MENU_CONTROL_PAUSE) == 6 &&
            menu_control_button(MENU_CONTROL_UP) == 11,
        "O is read from A, X from B and Pause from Start");

  menu = open_controls(&settings);
  check(MENU_GROUP_CONTROLS == MENU_GROUP_SHORTCUTS - 1 && menu.page == MENU_PAGE_CONTROLS &&
            menu.control_row == 0,
        "Controls comes before Shortcuts, and A on it opens its page");

  for (i = 0; i < MENU_CONTROL_O; i++) {
    press(&menu, MENU_DOWN);
  }
  press(&menu, MENU_A);
  check(menu_is_binding(&menu), "A on a control waits for its button");
  check(press(&menu, MENU_B) == MENU_STAY && menu_is_binding(&menu),
        "while waiting the menu's own buttons do nothing");
  menu_bind(&menu, 5, 0);
  check(menu_is_binding(&menu), "Menu itself cannot be bound");
  menu_bind(&menu, 9, 0);
  menu_binding_name(&settings.controls[MENU_CONTROL_O], name, sizeof name);
  check(!menu_is_binding(&menu) && strcmp(name, "L1") == 0, "a button pressed alone binds it alone");
  check(file_is(path, "up up\ndown down\nleft left\nright right\no l1\nx b\npause start\n"),
        "and every control is saved");

  press(&menu, MENU_DOWN);
  press(&menu, MENU_A);
  menu_bind(&menu, 9, 0);
  check(settings.controls[MENU_CONTROL_O].button == 9 && settings.controls[MENU_CONTROL_X].button == 9,
        "two controls can share a button, as PICO-8 can read O and X together");

  press(&menu, MENU_A);
  menu_bind(&menu, 3, 1);
  menu_binding_name(&settings.controls[MENU_CONTROL_X], name, sizeof name);
  check(strcmp(name, "MENU+Y") == 0, "Menu held with Y binds MENU+Y");

  press(&menu, MENU_A);
  check(press(&menu, MENU_MENU) == MENU_STAY && !menu_is_binding(&menu) &&
            settings.controls[MENU_CONTROL_X].button == 3,
        "a tap of Menu gives up waiting, leaving the control as it was");

  press(&menu, MENU_X);
  menu_binding_name(&settings.controls[MENU_CONTROL_X], name, sizeof name);
  check(strcmp(name, "None") == 0 &&
            file_is(path, "up up\ndown down\nleft left\nright right\no l1\nx none\npause start\n"),
        "X clears the control, which is saved as none");
  menu_read_controls(path, read_back);
  check(read_back[MENU_CONTROL_X].button == -1 && read_back[MENU_CONTROL_O].button == 9,
        "and reads back as no button");

  /* a shortcut on the button would keep it from the game, so the control takes it */
  settings.shortcuts[MENU_SHORTCUT_RESET].button = 10;
  settings.shortcuts[MENU_SHORTCUT_QUIT].button = 10;
  settings.shortcuts[MENU_SHORTCUT_QUIT].with_menu = 1;
  press(&menu, MENU_A);
  menu_bind(&menu, 10, 0);
  check(settings.controls[MENU_CONTROL_X].button == 10 &&
            settings.shortcuts[MENU_SHORTCUT_RESET].button == -1 &&
            settings.shortcuts[MENU_SHORTCUT_QUIT].button == 10,
        "binding a control takes the button from a shortcut, but not MENU+ it");
  check(file_is(shortcuts_path, "quit menu+r1\n"), "and saves the shortcuts too");

  press(&menu, MENU_DOWN);
  press(&menu, MENU_DOWN);
  check(menu.control_row == MENU_CONTROL_RESTORE, "Restore Defaults is the last row");
  press(&menu, MENU_X);
  check(file_is(path, "up up\ndown down\nleft left\nright right\no l1\nx r1\npause start\n"),
        "X does nothing on it");
  press(&menu, MENU_A);
  menu_binding_name(&settings.controls[MENU_CONTROL_O], name, sizeof name);
  check(!menu_is_binding(&menu) && strcmp(name, "A") == 0 &&
            settings.controls[MENU_CONTROL_X].button == 1 && access(path, F_OK) != 0,
        "A on it gives every control its button back and removes the file");
  press(&menu, MENU_DOWN);
  check(menu.control_row == 0, "Down from it wraps to the top");

  write_file(path, "o menu+x\npause menu+menu\nleft sideways\nnonsense a\nx\n");
  menu_read_controls(path, read_back);
  check(read_back[MENU_CONTROL_O].button == 2 && read_back[MENU_CONTROL_O].with_menu &&
            read_back[MENU_CONTROL_PAUSE].button == 6 && read_back[MENU_CONTROL_LEFT].button == 13 &&
            read_back[MENU_CONTROL_X].button == 1,
        "a binding to Menu, to no known button, or missing, keeps the control's own");
  write_file(path, "o l2\nx menu+r2\n");
  menu_read_controls(path, read_back);
  check(read_back[MENU_CONTROL_O].button == MENU_BUTTON_L2 &&
            read_back[MENU_CONTROL_X].button == MENU_BUTTON_R2 && read_back[MENU_CONTROL_X].with_menu,
        "the triggers can be bound to controls");

  check(press(&menu, MENU_B) == MENU_STAY && menu.page == MENU_PAGE_OPTIONS &&
            menu.option_row == MENU_GROUP_CONTROLS,
        "B goes back to Options on the Controls row");
  unlink(path);
  unlink(shortcuts_path);
}

static void test_control_drawing(const char *minui_font)
{
  struct menu_settings settings;
  struct menu menu;
  gfx_font *font = gfx_font_open(minui_font);
  gfx_theme theme;
  gfx_canvas canvas = guarded_canvas(640, 480);
  int row, inside = 1;

  settings_for_test(&settings);
  menu_default_controls(settings.controls);
  gfx_theme_init(&theme, "minui", NULL);
  menu = open_controls(&settings);
  for (row = 0; row < MENU_CONTROL_ROWS; row++) {
    menu_draw(&menu, &canvas, NULL, font, &theme, NULL);
    inside &= guards_intact(&canvas);
    press(&menu, MENU_DOWN);
  }
  press(&menu, MENU_A);
  menu_draw(&menu, &canvas, NULL, font, &theme, NULL);
  inside &= guards_intact(&canvas);
  check(inside && menu_is_binding(&menu),
        "the Controls page, waiting or not, stays inside the canvas");
  free(canvas.pixels - GUARD);
  gfx_font_close(font);
}

static void test_cycle_helpers(void)
{
  struct menu_settings settings;

  settings_for_test(&settings);
  menu_cycle_screen_mode(&settings);
  check(settings.screen_mode == MENU_SCREEN_MODE_STRETCHED && file_is(screen_mode_path, "stretched\n"),
        "the screen mode shortcut steps to stretched and saves it");
  menu_cycle_screen_mode(&settings);
  check(settings.screen_mode == 0, "and back to standard");

  menu_cycle_overlay(&settings);
  check(settings.overlay == 0 && file_is(overlay_path, "a.png\n"),
        "the overlay shortcut steps from None to the first");
  menu_cycle_overlay(&settings);
  menu_cycle_overlay(&settings);
  check(settings.overlay == -1 && file_is(overlay_path, "disabled\n"), "and past the last to None");
}

static char *shader_presets[] = {"crt.cfg", "smooth.cfg"};
static char *shader_programs[] = {"a.glsl", "b.glsl"};
static char shaders_dir[4096];
static char shader_programs_dir[4352];
static char shaders_path[4096];

/* NextUI's layout: presets in the folder and shaders in its glsl folder */
static void make_shader_fixtures(void)
{
  char path[8192];

  snprintf(shaders_dir, sizeof shaders_dir, "%s/Shaders", dir);
  make_dir(shaders_dir);
  snprintf(shader_programs_dir, sizeof shader_programs_dir, "%s/glsl", shaders_dir);
  make_dir(shader_programs_dir);
  snprintf(shaders_path, sizeof shaders_path, "%s/shaders", dir);

  snprintf(path, sizeof path, "%s/a.glsl", shader_programs_dir);
  write_file(path, "#pragma parameter SCANLINE \"Scanline Strength\" 0.5 0.0 1.0 0.1\n"
                   "#pragma parameter MASK \"Mask\" 1.0 0.0 3.0 1.0\n"
                   "void main() {}\n");
  snprintf(path, sizeof path, "%s/b.glsl", shader_programs_dir);
  write_file(path, "#pragma parameter MASK \"Mask\" 1.0 0.0 3.0 1.0\n"
                   "#pragma parameter CURVE \"Curvature\" 0.2 0.0 0.5 0.05\n"
                   "void main() {}\n");
  /* as NextUI ships them, with no newline at the end */
  snprintf(path, sizeof path, "%s/crt.cfg", shaders_dir);
  write_file(path, "minarch_nrofshaders = 2\nminarch_shader1 = a.glsl\n"
                   "minarch_shader1_upscale = screen\nminarch_shader2 = b.glsl\n"
                   "minarch_shader2_filter = LINEAR\nminarch_scale_filter = LINEAR");
  snprintf(path, sizeof path, "%s/smooth.cfg", shaders_dir);
  write_file(path, "minarch_nrofshaders = 1\nminarch_shader1 = b.glsl\nCURVE = 0.40");
}

static void shader_settings_for_test(struct menu_settings *settings)
{
  settings_for_test(settings);
  unlink(shaders_path);
  settings->shaders_path = shaders_path;
  shader_read_settings(&settings->shaders, shaders_path);
  settings->shader_presets_dir = shaders_dir;
  settings->shader_programs_dir = shader_programs_dir;
  settings->shader_presets = shader_presets;
  settings->shader_preset_count = 2;
  settings->shader_programs = shader_programs;
  settings->shader_program_count = 2;
}

/* whether the saved shaders hold line, which is one of minarch's key = value lines */
static int shaders_saved(const char *line)
{
  char text[4096] = "", wanted[256];
  FILE *file = fopen(shaders_path, "r");
  size_t got;

  if (file == NULL) {
    return 0;
  }
  got = fread(text, 1, sizeof text - 1, file);
  text[got] = '\0';
  fclose(file);
  snprintf(wanted, sizeof wanted, "%s\n", line);
  return strstr(text, wanted) != NULL;
}

static void test_shaders(void)
{
  struct menu_settings settings;
  struct menu menu;
  char value[64];
  int row;

  make_shader_fixtures();

  /* without shaders there is only the preset, which stays None */
  settings_for_test(&settings);
  settings.shaders_path = shaders_path;
  menu = open_options(&settings);
  check(menu_group_size(&menu, MENU_GROUP_SHADERS) == 1 &&
            menu_group_setting(&menu, MENU_GROUP_SHADERS, 0) == MENU_SHADER_PRESET,
        "with no shaders the Shaders group holds only the preset");
  select_setting(&menu, MENU_SHADER_PRESET);
  check(menu.option_row == MENU_GROUP_SHADERS, "Shaders comes after Emulator");
  press(&menu, MENU_RIGHT);
  menu_option_value(&menu, MENU_SHADER_PRESET, value, sizeof value);
  check(strcmp(value, "None") == 0 && access(shaders_path, F_OK) != 0,
        "which shows None and changes nothing");

  shader_settings_for_test(&settings);
  settings.shaders_unavailable = 1;
  menu = open_options(&settings);
  check(menu_group_size(&menu, MENU_GROUP_SHADERS) == 1,
        "nor are there more rows when pico-8's renderer cannot draw them");

  shader_settings_for_test(&settings);
  menu = open_options(&settings);
  check(menu_group_size(&menu, MENU_GROUP_SHADERS) == 3 &&
            menu_group_setting(&menu, MENU_GROUP_SHADERS, 1) == MENU_SHADER_PASSES &&
            menu_group_setting(&menu, MENU_GROUP_SHADERS, 2) == MENU_SHADER_SHARPNESS,
        "with shaders but none in use it holds the preset, their number and the sharpness");
  select_setting(&menu, MENU_SHADER_PRESET);
  press(&menu, MENU_RIGHT);
  menu_option_value(&menu, MENU_SHADER_PRESET, value, sizeof value);
  check(strcmp(value, "crt") == 0 && settings.shaders.passes == 2 &&
            strcmp(settings.shaders.pass[1].program, "b.glsl") == 0 &&
            settings.shaders.pass[1].filter == SHADER_LINEAR &&
            settings.shaders.pass[0].upscale == SHADER_UPSCALE_SCREEN &&
            settings.shaders.scale_filter == SHADER_LINEAR,
        "Right picks the first preset, shown without .cfg, and reads it over the shaders");
  check(shaders_saved("minarch_shaders_preset = crt.cfg") && shaders_saved("minarch_nrofshaders = 2") &&
            shaders_saved("minarch_shader2_filter = LINEAR") &&
            shaders_saved("minarch_shader1_upscale = screen"),
        "and saves them as minarch's keys");
  check(menu_group_size(&menu, MENU_GROUP_SHADERS) == 3 + 2 * MENU_SHADER_PASS_ROWS + 1 &&
            menu_group_setting(&menu, MENU_GROUP_SHADERS, 3) == MENU_SHADER1_PROGRAM &&
            menu_group_setting(&menu, MENU_GROUP_SHADERS, 8) == MENU_SHADER1_PROGRAM +
                                                                   MENU_SHADER_PASS_ROWS &&
            menu_group_setting(&menu, MENU_GROUP_SHADERS, 13) == MENU_SHADER_PARAMETERS,
        "each pass in use adds its five rows, and Parameters follows them");
  check(settings.shader_param_count == 3 && strcmp(settings.shader_params[0].name, "SCANLINE") == 0 &&
            strcmp(settings.shader_params[1].name, "MASK") == 0 &&
            strcmp(settings.shader_params[2].name, "CURVE") == 0,
        "Parameters lists each shader's parameters once");

  select_setting(&menu, MENU_SHADER1_PROGRAM + MENU_SHADER_PASS_ROWS + 4);
  menu_option_value(&menu, MENU_SHADER1_PROGRAM + MENU_SHADER_PASS_ROWS + 4, value, sizeof value);
  check(strcmp(value, "1") == 0, "Shader 2 Scale shows the 1x it starts at");
  press(&menu, MENU_LEFT);
  check(settings.shaders.pass[1].upscale == SHADER_UPSCALE_SCREEN &&
            shaders_saved("minarch_shader2_upscale = screen") &&
            shaders_saved("minarch_shaders_preset = crt.cfg"),
        "Left wraps a pass's scale to the screen, keeping the preset's name as minarch does");

  select_setting(&menu, MENU_SHADER1_PROGRAM);
  press(&menu, MENU_RIGHT);
  menu_option_value(&menu, MENU_SHADER1_PROGRAM, value, sizeof value);
  check(strcmp(value, "b") == 0 && shaders_saved("minarch_shader1 = b.glsl") &&
            settings.shader_param_count == 2,
        "Right steps a pass's shader, shown without .glsl, and reads its parameters again");
  press(&menu, MENU_LEFT);

  select_setting(&menu, MENU_SHADER_PARAMETERS);
  press(&menu, MENU_A);
  check(menu.page == MENU_PAGE_SHADER_PARAMS && menu.param_row == 0,
        "A on Parameters opens their page");
  press(&menu, MENU_RIGHT);
  check(shader_param_value(&settings.shaders, &settings.shader_params[0]) > 0.55f &&
            shaders_saved("SCANLINE = 0.60"),
        "Right steps a parameter by its step and saves it by name");
  for (row = 0; row < 5; row++) {
    press(&menu, MENU_RIGHT);
  }
  check(shaders_saved("SCANLINE = 0.00"), "and past its end wraps to its start");
  press(&menu, MENU_DOWN);
  press(&menu, MENU_LEFT);
  check(shaders_saved("MASK = 0.00"), "Left steps the next one down");
  check(press(&menu, MENU_B) == MENU_STAY && menu.page == MENU_PAGE_GROUP,
        "B goes back to the Shaders group");

  select_setting(&menu, MENU_SHADER_PASSES);
  press(&menu, MENU_LEFT);
  check(settings.shaders.passes == 1 && menu_group_size(&menu, MENU_GROUP_SHADERS) ==
                                            3 + MENU_SHADER_PASS_ROWS + 1,
        "one shader fewer leaves one pass's rows");
  menu_option_value(&menu, MENU_SHADER_PRESET, value, sizeof value);
  check(strcmp(value, "crt") == 0, "and the preset its name");

  select_setting(&menu, MENU_SHADER_PRESET);
  press(&menu, MENU_LEFT);
  check(settings.shaders.passes == 0 && shaders_saved("minarch_nrofshaders = off") &&
            shaders_saved("minarch_shaders_preset = ") && shaders_saved("minarch_shader1 = a.glsl"),
        "Left to None turns the shaders off and keeps the passes");
  check(menu_group_size(&menu, MENU_GROUP_SHADERS) == 3 && !menu_shaders_on(&settings),
        "which takes their rows away");

  select_setting(&menu, MENU_SHADER_PASSES);
  press(&menu, MENU_RIGHT);
  menu_option_value(&menu, MENU_SHADER_PRESET, value, sizeof value);
  check(strcmp(value, "Custom") == 0 && menu_shaders_on(&settings),
        "shaders turned on by hand show as Custom");
  select_setting(&menu, MENU_SHADER_PRESET);
  press(&menu, MENU_RIGHT);
  menu_option_value(&menu, MENU_SHADER_PRESET, value, sizeof value);
  check(strcmp(value, "crt") == 0, "and Right from there picks the first preset");
}

static void test_shader_drawing(void)
{
  struct menu_settings settings;
  struct menu menu;
  gfx_canvas canvas = guarded_canvas(640, 480);
  gfx_font *font = gfx_font_open(NULL);
  gfx_theme theme;
  int row, count;

  gfx_theme_init(&theme, "nextui", NULL);
  shader_settings_for_test(&settings);
  shader_apply_preset(&settings.shaders, shaders_dir, "crt.cfg");
  menu = open_options(&settings);
  select_setting(&menu, MENU_SHADER_PRESET);
  count = menu_group_size(&menu, MENU_GROUP_SHADERS);
  for (row = 0; row < count; row++) {
    menu_draw(&menu, &canvas, NULL, font, &theme, NULL);
    press(&menu, MENU_DOWN);
  }
  select_setting(&menu, MENU_SHADER_PARAMETERS);
  press(&menu, MENU_A);
  for (row = 0; row < settings.shader_param_count; row++) {
    menu_draw(&menu, &canvas, NULL, font, &theme, NULL);
    press(&menu, MENU_DOWN);
  }
  check(menu.page == MENU_PAGE_SHADER_PARAMS && guards_intact(&canvas),
        "the Shaders group and its parameters stay inside the canvas");
  gfx_font_close(font);
  free(canvas.pixels - GUARD);
}

static void test_cycle_shader(void)
{
  struct menu_settings settings;
  char shown[64];

  shader_settings_for_test(&settings);
  menu_cycle_shader(&settings, shown, sizeof shown);
  check(strcmp(shown, "Shader: crt") == 0 && settings.shaders.passes == 2 &&
            shaders_saved("minarch_shaders_preset = crt.cfg"),
        "the shader shortcut steps from None to the first preset and saves it");
  menu_cycle_shader(&settings, shown, sizeof shown);
  check(strcmp(shown, "Shader: smooth") == 0 && settings.shaders.passes == 1 &&
            shaders_saved("CURVE = 0.40"),
        "then to the next, with the parameters it sets");
  menu_cycle_shader(&settings, shown, sizeof shown);
  check(strcmp(shown, "Shader: None") == 0 && settings.shaders.passes == 0,
        "and past the last to None");

  settings.shader_preset_count = 0;
  menu_cycle_shader(&settings, shown, sizeof shown);
  check(strcmp(shown, "No shader presets") == 0 && settings.shaders.passes == 0,
        "with no presets it says so and changes nothing");
  settings.shader_preset_count = 2;
  settings.shaders_unavailable = 1;
  menu_cycle_shader(&settings, shown, sizeof shown);
  check(strcmp(shown, "Shaders need OpenGL ES") == 0 && settings.shaders.passes == 0,
        "and when pico-8's renderer cannot draw them, says why");
}

static void test_shortcut_drawing(const char *minui_font)
{
  struct menu_settings settings;
  struct menu menu;
  gfx_font *font = gfx_font_open(minui_font);
  gfx_theme theme;
  gfx_canvas canvas = guarded_canvas(640, 480);
  gfx_canvas pill;
  int w, h;

  settings_for_test(&settings);
  gfx_theme_init(&theme, "minui", NULL);
  menu = open_options(&settings);
  press(&menu, MENU_UP);
  press(&menu, MENU_A);
  menu_draw(&menu, &canvas, NULL, font, &theme, NULL);
  press(&menu, MENU_A);
  menu_draw(&menu, &canvas, NULL, font, &theme, NULL);
  check(guards_intact(&canvas), "the Shortcuts page, waiting or not, stays inside the canvas");
  free(canvas.pixels - GUARD);

  menu_notification_size(1024, 768, font, "Screenshot saved", &w, &h);
  check(h == 90 && w > 72, "a notice is a pill as tall as the title's, as wide as its text");
  pill = guarded_canvas(w, h);
  gfx_clear(&pill, 0);
  menu_draw_notification(&pill, 1024, 768, &theme, font, 0, 0, "Screenshot saved");
  check(guards_intact(&pill) && pill.pixels[0] == 0 &&
            pill.pixels[(size_t)(h / 2) * w + w / 2] != 0,
        "and is drawn inside it, with clear corners");
  free(pill.pixels - GUARD);
  gfx_font_close(font);
}

static void test_screenshot_file(void)
{
  char folder[8192], cart[16384], written[16384], expected[32768];
  static uint32_t pixels[4 * 3];
  unsigned char png[24];
  struct tm when = {0};
  time_t at;
  FILE *file;
  int i, ok;

  for (i = 0; i < 12; i++) {
    pixels[i] = 0xff000000u | (uint32_t)i;
  }
  snprintf(folder, sizeof folder, "%s/shots/Pico-8 (PICO)", dir);
  put_file(folder, "1) Celeste.p8", "cart");
  snprintf(cart, sizeof cart, "%s/1) Celeste.p8", folder);
  when.tm_year = 2026 - 1900;
  when.tm_mon = 9;
  when.tm_mday = 24;
  when.tm_hour = 13;
  when.tm_min = 5;
  when.tm_sec = 9;
  when.tm_isdst = -1;
  at = mktime(&when);

  snprintf(folder, sizeof folder, "%s/shots/Screenshots", dir);
  check(cart_write_screenshot(folder, cart, pixels, 4, 3, at, written, sizeof written),
        "a screenshot is written, making its folder");
  snprintf(expected, sizeof expected, "%s/Celeste.2026-10-24-13-05-09.png", folder);
  check(strcmp(written, expected) == 0, "named for the game and when, as NextUI names its own");

  file = fopen(written, "rb");
  ok = file != NULL && fread(png, 1, sizeof png, file) == sizeof png;
  if (file != NULL) {
    fclose(file);
  }
  check(ok && memcmp(png, "\x89PNG\r\n\x1a\n", 8) == 0 && png[19] == 4 && png[23] == 3,
        "as a PNG the size of the screen");
  snprintf(expected, sizeof expected, "%s.part", written);
  check(access(expected, F_OK) != 0, "with nothing left under the temporary name");
}

static void test_splore_covers(void)
{
  char covers[8192], roms[8192], folder[16384], other[16384], path[32768];
  struct menu_settings settings;
  struct menu menu;

  snprintf(covers, sizeof covers, "%s/covers", dir);
  put_file(covers, "Splore.p8.png", "standard cover");
  put_file(covers, "Splore-fancy.p8.png", "fancy cover");

  snprintf(roms, sizeof roms, "%s/covers-roms", dir);
  snprintf(folder, sizeof folder, "%s/Pico-8 (PICO)", roms);
  put_file(folder, "1) Splore.p8", "cart");
  put_file(folder, "Celeste.p8", "cart");
  snprintf(path, sizeof path, "%s/.media", folder);
  put_file(path, "1) Splore.png", "standard cover");
  put_file(path, "Celeste.png", "standard cover");
  snprintf(path, sizeof path, "%s/.res", folder);
  put_file(path, "1) Splore.p8.png", "my own cover");
  snprintf(other, sizeof other, "%s/Game Boy (GB)", roms);
  put_file(other, "Splore.gb", "rom");
  snprintf(path, sizeof path, "%s/.media", other);
  put_file(path, "Splore.png", "standard cover");

  settings_for_test(&settings);
  settings.roms_dir = roms;
  settings.tag = "PICO";
  settings.splore_covers_dir = covers;
  menu = open_options(&settings);
  select_setting(&menu, MENU_SPLORE_ARTWORK);
  press(&menu, MENU_RIGHT);

  snprintf(path, sizeof path, "%s/.media/1) Splore.png", folder);
  check(same_text(path, "fancy cover"), "the menu gives a renamed Splore cart the fancy cover at once");
  snprintf(path, sizeof path, "%s/.res/1) Splore.p8.png", folder);
  check(same_text(path, "my own cover"), "a cover of the user's own is left alone");
  snprintf(path, sizeof path, "%s/.media/Celeste.png", folder);
  check(same_text(path, "standard cover"), "a cart without Splore in its name is left alone");
  snprintf(path, sizeof path, "%s/.media/Splore.png", other);
  check(same_text(path, "standard cover"), "as is a roms folder for another emulator");

  press(&menu, MENU_RIGHT);
  snprintf(path, sizeof path, "%s/.media/1) Splore.png", folder);
  check(same_text(path, "standard cover"), "stepping back gives it the standard cover again");
  check(cart_apply_splore_artwork(roms, "PICO", covers, 0) == 0,
        "a cover already set is not written again");
  snprintf(path, sizeof path, "%s/missing", dir);
  check(cart_apply_splore_artwork(roms, "PICO", path, 1) == 0,
        "nothing changes when the pak's covers are missing");
  unlink(splore_artwork_path);
}

/* logs one line with stderr sent to a file, and reads it back */
static void capture_log(const char *message, char *line, size_t size)
{
  char path[8192];
  int saved = dup(fileno(stderr));
  FILE *file;

  snprintf(path, sizeof path, "%s/log", dir);
  fflush(stderr);
  if (saved < 0 || freopen(path, "w", stderr) == NULL) {
    perror(path);
    exit(2);
  }
  gfx_log("%s\n", message);
  fflush(stderr);
  dup2(saved, fileno(stderr));
  close(saved);

  line[0] = '\0';
  file = fopen(path, "r");
  if (file != NULL) {
    if (fgets(line, (int)size, file) == NULL) {
      line[0] = '\0';
    }
    fclose(file);
  }
}

static void test_log(void)
{
  char path[8192], line[256];

  snprintf(path, sizeof path, "%s/uptime", dir);
  write_file(path, "1234.56 7890.12\n");
  setenv("PICO_PAK_UPTIME_FILE", path, 1);
  capture_log("opening the menu", line, sizeof line);
  check(strcmp(line, "[1234.56] sdl-display: opening the menu\n") == 0,
        "the shim stamps its log with the seconds since boot, as launch.sh does");

  snprintf(path, sizeof path, "%s/no-uptime", dir);
  setenv("PICO_PAK_UPTIME_FILE", path, 1);
  capture_log("closing the menu", line, sizeof line);
  check(strcmp(line, "[?] sdl-display: closing the menu\n") == 0,
        "a line still logs when the time cannot be read");
  unsetenv("PICO_PAK_UPTIME_FILE");
}

static void test_gfx(void)
{
  gfx_metrics brick = gfx_metrics_for(1024, 768), smart_pro = gfx_metrics_for(1280, 720);
  gfx_metrics small = gfx_metrics_for(320, 240);

  check(brick.k == 3.0 && brick.padding == 5, "the Brick is drawn at minarch's scale 3");
  check(smart_pro.k == 2.0 && smart_pro.padding == 10, "the Smart Pro is drawn at scale 2");
  check(small.k == 0.75 && gfx_u(&small, GFX_PILL_SIZE) == 23,
        "a screen under 480 lines gets a fraction of a unit");

  check(gfx_blend(GFX_BLACK, GFX_WHITE) == GFX_WHITE, "an opaque colour covers what is under it");
  check(gfx_blend(GFX_BLACK, 0x00ffffff) == GFX_BLACK, "a transparent colour leaves it alone");
  check(gfx_blend(GFX_BLACK, 0x80ffffff) == GFX_ARGB(0x808080), "half white over black is grey");
}

/* with save states, Save and Load can be chosen and Left and Right step through the slots */
static void test_slots(void)
{
  struct menu_settings settings;
  struct menu menu;

  settings_for_test(&settings);
  menu_open(&menu, &settings, "Cart");
  check(!menu.states.available, "a menu opens with no save states until they are filled in");
  menu.states.available = 1;
  menu.states.exists[2] = 1;

  press(&menu, MENU_RIGHT);
  check(menu.states.slot == 0, "Right on Continue changes no slot");
  press(&menu, MENU_DOWN);
  check(menu.root_row == MENU_SAVE, "with save states Down stops on Save");
  press(&menu, MENU_RIGHT);
  check(menu.states.slot == 1 && menu.dirty, "Right on Save goes to the next slot");
  press(&menu, MENU_LEFT);
  press(&menu, MENU_LEFT);
  check(menu.states.slot == MENU_SLOTS - 1, "Left from the first slot wraps to the last");
  press(&menu, MENU_RIGHT);
  check(menu.states.slot == 0, "and Right from the last back to the first");
  check(press(&menu, MENU_A) == MENU_SAVE_STATE && menu.states.slot == 0,
        "A on Save saves to the slot shown");

  press(&menu, MENU_DOWN);
  check(menu.root_row == MENU_LOAD, "Load comes after Save");
  check(press(&menu, MENU_A) == MENU_STAY, "A on Load does nothing for an empty slot");
  press(&menu, MENU_RIGHT);
  press(&menu, MENU_RIGHT);
  check(press(&menu, MENU_A) == MENU_LOAD_STATE && menu.states.slot == 2,
        "and loads a slot that holds a state");
  press(&menu, MENU_DOWN);
  check(menu.root_row == MENU_OPTIONS, "Options follows Load");
}

static void test_slot_drawing(const char *minui_font)
{
  /* the Brick's scale is 3: a frame of 12 around half the screen, and 54 more below it */
  int x = 1024 - 536 - 15 + 12, y = (768 - 438) / 2 + 12;
  gfx_canvas canvas = guarded_canvas(1024, 768);
  gfx_font *font = gfx_font_open(minui_font);
  struct menu_settings settings;
  uint32_t *frame = malloc((size_t)1024 * 768 * sizeof *frame);
  uint32_t preview[4 * 3];
  gfx_theme theme;
  struct menu menu;
  size_t i;

  for (i = 0; i < (size_t)1024 * 768; i++) {
    frame[i] = GFX_ARGB(0xc0c0c0);
  }
  for (i = 0; i < 12; i++) {
    preview[i] = GFX_ARGB(0x123456);
  }
  preview[11] = GFX_ARGB(0x654321);
  gfx_theme_init(&theme, "minui", NULL);
  settings_for_test(&settings);
  menu_open(&menu, &settings, "Cart");
  menu.states.available = 1;
  menu.states.exists[1] = 1;
  menu.states.preview[1] = preview;
  menu.states.preview_w[1] = 4;
  menu.states.preview_h[1] = 3;

  menu_draw(&menu, &canvas, frame, font, &theme, NULL);
  check(canvas.pixels[(size_t)(y + 192) * 1024 + x + 256] == GFX_ARGB(0x5f5f5f),
        "no slot is shown on Continue");

  press(&menu, MENU_DOWN);
  press(&menu, MENU_RIGHT);
  menu_draw(&menu, &canvas, frame, font, &theme, NULL);
  check(guards_intact(&canvas), "the slot stays inside the canvas");
  check(canvas.pixels[(size_t)(y + 192) * 1024 + x + 256] == GFX_ARGB(0x123456),
        "a slot's preview fills half the screen at the right");
  check(canvas.pixels[(size_t)(y + 383) * 1024 + x + 511] == GFX_ARGB(0x654321),
        "stretched to fit, its last pixel at the corner");
  check(canvas.pixels[(size_t)(y + 192) * 1024 + x - 6] == GFX_WHITE, "inside a white frame");

  press(&menu, MENU_LEFT);
  menu_draw(&menu, &canvas, frame, font, &theme, NULL);
  check(canvas.pixels[(size_t)(y + 2) * 1024 + x + 2] == GFX_BLACK &&
            has_colour(&canvas, x, y + 150, 512, 84, GFX_WHITE),
        "an empty slot is black with its message in the middle");

  press(&menu, MENU_RIGHT);
  menu.states.saving[1] = 1;
  menu_draw(&menu, &canvas, frame, font, &theme, NULL);
  check(canvas.pixels[(size_t)(y + 192) * 1024 + x + 256] != GFX_ARGB(0x123456) &&
            has_colour(&canvas, x, y + 150, 512, 84, GFX_WHITE),
        "a slot still being written says so rather than showing what it held");

  gfx_font_close(font);
  free(frame);
  free(canvas.pixels - GUARD);
}

static void test_auto_save(void)
{
  char path[8192];
  struct menu_settings settings;
  struct menu menu;

  snprintf(path, sizeof path, "%s/auto-save-on-exit", dir);
  check(!menu_read_auto_save(path), "Auto Save on Exit is off without its file");
  write_file(path, "yes\n");
  check(!menu_read_auto_save(path), "and off for anything but on");

  settings_for_test(&settings);
  settings.auto_save_path = path;
  menu = open_options(&settings);
  select_setting(&menu, MENU_AUTO_SAVE);
  check(menu.option_row == MENU_GROUP_EMULATOR, "it is one of the Emulator settings");
  press(&menu, MENU_A);
  check(settings.auto_save && file_is(path, "on\n") && menu_read_auto_save(path),
        "turning it on saves on to its file");
  press(&menu, MENU_LEFT);
  check(!settings.auto_save && file_is(path, "off\n"), "and turning it off saves off");
}

static void test_toast(const char *nextui_font)
{
  gfx_font *font = gfx_font_open(nextui_font);
  gfx_theme minui, nextui;
  gfx_canvas canvas;
  int w, h, other_w, other_h;

  gfx_theme_init(&minui, "minui", NULL);
  gfx_theme_init(&nextui, "nextui", ",0x112233FF,,,,,");
  check(gfx_toast_pill(&minui) == minui.color[1] && gfx_toast_pill(&nextui) == GFX_ARGB(0x112233),
        "the toast is NextUI's accent under either launcher");
  check(menu_toast_margin(1024, 768) == 36, "and sits 12 units in from the corner");

  menu_toast_size(1024, 768, font, "Saving...", &w, &h);
  menu_toast_size(1024, 768, font, "Saved", &other_w, &other_h);
  check(w > other_w && h == other_h && h > 24, "its width follows its text, padded 8 by 4 units");

  canvas = guarded_canvas(w, h);
  gfx_clear(&canvas, 0);
  menu_draw_toast(&canvas, 1024, 768, &nextui, font, 0, 0, "Saving...");
  check(guards_intact(&canvas), "the toast stays inside its canvas");
  check(canvas.pixels[(size_t)(h / 2) * w + 4] == gfx_toast_pill(&nextui) && canvas.pixels[0] == 0,
        "a pill, its corners left clear for the game");
  check(has_colour(&canvas, 0, 0, w, h, gfx_toast_text(&nextui)), "with its text in NextUI's colour 1");

  free(canvas.pixels - GUARD);
  gfx_font_close(font);
}

static void test_state_files(void)
{
  char roms[8192], switcher[8192], states[8192], cart[16384], path[16384], expected[16384];
  uint32_t frame[2 * 3] = {0};

  snprintf(roms, sizeof roms, "%s/states/Pico-8 (PICO)", dir);
  snprintf(switcher, sizeof switcher, "%s/states/.minui/PICO", dir);
  snprintf(states, sizeof states, "%s/states/saves", dir);
  put_file(roms, "Celeste.p8", "cart");
  snprintf(cart, sizeof cart, "%s/Celeste.p8", roms);

  snprintf(expected, sizeof expected, "%s/Celeste.p8.st3", states);
  check(cart_state_path(states, cart, 3, path, sizeof path) && strcmp(path, expected) == 0,
        "a slot's state is named as minarch names its own");
  check(!cart_state_path(NULL, cart, 3, path, sizeof path) &&
            !cart_state_path("", cart, 3, path, sizeof path),
        "and has no path without a folder, as in Splore");
  snprintf(expected, sizeof expected, "%s/Celeste.p8.3.bmp", switcher);
  check(cart_preview_path(switcher, cart, 3, path, sizeof path) && strcmp(path, expected) == 0,
        "its preview goes where the switcher looks");

  check(cart_read_slot(switcher, cart) == -1, "a cart with no .txt has no slot to resume");
  check(cart_write_slot(switcher, cart, 5) && cart_read_slot(switcher, cart) == 5,
        "the slot a Load names is the one read back");
  snprintf(path, sizeof path, "%s/Celeste.p8.txt", switcher);
  check(same_text(path, "5\n"), "written as minarch writes it");

  check(!cart_has_state(states, cart), "a cart with no state file has no state");
  put_file(states, "Celeste.p8.st9", "state");
  check(cart_has_state(states, cart), "the resume slot counts as a state");

  check(cart_write_preview_frame(switcher, cart, frame, 2, 3, 4), "a slot's preview is written");
  snprintf(path, sizeof path, "%s/Celeste.p8.4.bmp", switcher);
  check(access(path, F_OK) == 0 && cart_read_slot(switcher, cart) == 4,
        "under the slot's name, which becomes the one to resume");

  snprintf(path, sizeof path, "%s/Poom", roms);
  put_file(path, "Poom.m3u", "poom_0.p8\n");
  snprintf(cart, sizeof cart, "%s/Poom/poom_0.p8", roms);
  snprintf(expected, sizeof expected, "%s/Poom.m3u.st1", states);
  check(cart_state_path(states, cart, 1, path, sizeof path) && strcmp(path, expected) == 0,
        "a multi-cart game's states are keyed by its m3u");
}

int main(int argc, char **argv)
{
  const char *minui_font = argc > 1 ? argv[1] : "";
  const char *nextui_font = argc > 2 ? argv[2] : "";

  make_fixtures();
  test_root();
  test_repeat();
  test_screen_mode_and_overlay();
  test_copy_carts();
  test_splore_artwork();
  test_description_lines();
  test_config();
  test_drawing(minui_font, nextui_font);
  test_indicator_drawing();
  test_scrolling();
  test_fonts(minui_font, nextui_font);
  test_theme();
  test_battery();
  test_gfx();
  test_log();
  test_cart_title();
  test_switcher_preview();
  test_preview_size();
  test_splore_covers();
  test_shortcuts_page();
  test_controls_page();
  test_control_drawing(minui_font);
  test_cycle_helpers();
  test_shaders();
  test_shader_drawing();
  test_cycle_shader();
  test_shortcut_drawing(minui_font);
  test_screenshot_file();
  test_slots();
  test_slot_drawing(minui_font);
  test_toast(nextui_font);
  test_auto_save();
  test_state_files();

  printf("1..%d\n", tests);
  return failures == 0 ? 0 : 1;
}
