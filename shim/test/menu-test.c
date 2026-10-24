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
static char *overlay_names[] = {"a.png", "b.png"};

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

static void select_row(struct menu *menu, int row)
{
  while (menu->option_row != row) {
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

  select_row(&menu, MENU_OVERLAY);
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

  select_row(&menu, MENU_COPY_CARTS);
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
  menu_option_value(&menu, MENU_CONFIG_FIRST, value, sizeof value);
  check(strcmp(value, "On") == 0, "a setting the menu saved wins over config.txt");
  menu_option_value(&menu, MENU_CONFIG_FIRST + 1, value, sizeof value);
  check(strcmp(value, "50%") == 0, "a setting never changed is read from config.txt");
  menu_option_value(&menu, MENU_CONFIG_FIRST + 2, value, sizeof value);
  check(strcmp(value, "Off") == 0, "a value the menu does not offer shows the default");
  menu_option_value(&menu, MENU_CONFIG_FIRST + 4, value, sizeof value);
  check(strcmp(value, "On") == 0, "a setting in neither file shows the default");

  select_row(&menu, MENU_CONFIG_FIRST + 1);
  press(&menu, MENU_RIGHT);
  check(file_is(pico8_config_path, "show_fps 1\nsplore_filter 1\nvolume 192\n"),
        "changing a setting adds it and keeps the others");
  select_row(&menu, MENU_CONFIG_FIRST);
  press(&menu, MENU_LEFT);
  check(file_is(pico8_config_path, "show_fps 0\nsplore_filter 1\nvolume 192\n"),
        "changing it again replaces its line");
  select_row(&menu, MENU_CONFIG_FIRST + 2);
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
        int row;

        gfx_theme_init(&theme, styles[t], NULL);
        menu_open(&menu, &settings,
                  "A cart whose name is far too long to fit in the title at all, ever");
        menu_draw(&menu, &canvas, frame, font, &theme, &batteries[f]);
        all_inside &= guards_intact(&canvas);
        selected_drawn &= canvas.pixels[(size_t)(top + pill / 2) * w + gfx_u(&m, m.padding) + 2] ==
                          gfx_selected_pill(&theme);
        dimmed &= canvas.pixels[(size_t)(h / 2) * w + w - 1] == GFX_ARGB(0x5f5f5f);

        press(&menu, MENU_DOWN);
        press(&menu, MENU_A);
        for (row = 0; row < MENU_OPTION_COUNT; row++) {
          menu_draw(&menu, &canvas, frame, font, &theme, &batteries[f]);
          all_inside &= guards_intact(&canvas);
          cleared &= canvas.pixels[(size_t)(h - 1) * w] == gfx_background(&theme);
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

static void test_scrolling(void)
{
  struct menu_settings settings;
  struct menu menu;
  gfx_canvas canvas = guarded_canvas(640, 480);
  gfx_font *font = gfx_font_open(NULL);
  gfx_theme theme;

  settings_for_test(&settings);
  gfx_theme_init(&theme, "minui", NULL);
  menu = open_options(&settings);
  select_row(&menu, MENU_OPTION_COUNT - 1);
  menu_draw(&menu, &canvas, NULL, font, &theme, NULL);
  check(menu.option_top > 0, "the last row scrolls the list on a 480 line screen");
  press(&menu, MENU_DOWN);
  menu_draw(&menu, &canvas, NULL, font, &theme, NULL);
  check(menu.option_row == 0 && menu.option_top == 0, "Down from the last row wraps to the top");
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

int main(int argc, char **argv)
{
  const char *minui_font = argc > 1 ? argv[1] : "";
  const char *nextui_font = argc > 2 ? argv[2] : "";

  make_fixtures();
  test_root();
  test_repeat();
  test_screen_mode_and_overlay();
  test_copy_carts();
  test_config();
  test_drawing(minui_font, nextui_font);
  test_scrolling();
  test_fonts(minui_font, nextui_font);
  test_theme();
  test_battery();
  test_gfx();

  printf("1..%d\n", tests);
  return failures == 0 ? 0 : 1;
}
