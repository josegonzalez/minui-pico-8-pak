/*
 * Plays pico-8's part against sdl-display.so, which is preloaded over the fake
 * libSDL2 in fake-sdl.c, and checks what reached the fake:
 *
 *   LD_PRELOAD=sdl-display.so display-test [quit-event]
 *
 * The menu holds pico-8 inside SDL_RenderPresent, so each menu is driven by a
 * script of pad states the fake steps through on every SDL_Delay. The shim
 * reads its environment once, so the tests run in one process in order, apart
 * from quitting on an SDL_QUIT, which like any quit cannot be undone and so has
 * a run of its own. Prints TAP and exits non-zero on a failure.
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <linux/input.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fake-sdl.h"

/* From SDL_events.h, SDL_pixels.h and SDL_gamecontroller.h. */
#define SDL_QUIT 0x100
#define SDL_KEYDOWN 0x300
#define SDL_CONTROLLERBUTTONDOWN 0x650
#define SDL_PIXELFORMAT_ARGB8888 0x16362004
#define BUTTON_A 0
#define BUTTON_B 1
#define BUTTON_BACK 4
#define BUTTON_GUIDE 5
#define BUTTON_START 6
#define BUTTON_DPAD_UP 11
#define BUTTON_DPAD_DOWN 12
#define BUTTON_DPAD_LEFT 13
#define BUTTON_DPAD_RIGHT 14

#define BIT(button) (1u << (button))

/* the menu's Options rows, in order */
enum { ROW_SCREEN_MODE, ROW_OVERLAY, ROW_COPY_CARTS, ROW_SPLORE_ARTWORK, ROW_SHOW_FPS, ROW_VOLUME };

/* what pico-8 calls, answered by the shim and passed on to the fake */
void SDL_RenderPresent(void *renderer);
void SDL_DestroyRenderer(void *renderer);
int SDL_PollEvent(void *event);
int SDL_RenderCopy(void *renderer, void *texture, const fake_rect *src, const fake_rect *dst);
void *SDL_GameControllerOpen(int index);
void SDL_GameControllerClose(void *controller);
uint8_t SDL_GameControllerGetButton(void *controller, int button);
uint32_t SDL_OpenAudioDevice(const char *device, int iscapture, const void *desired,
                             void *obtained, int allowed_changes);
int SDL_GetModState(void);

static int tests;
static int failures;
static char dir[] = "/tmp/display-test.XXXXXX";
static char choice_path[4096];
static char screen_mode_path[4096];
static char copy_carts_dir[4096];
static char pico8_config_path[4096];
static char splore_artwork_path[4096];
static char switcher_dir[4096];
static char shortcuts_path[4096];
static char screenshots_dir[4096];

static int preview_is_frame(int w, int h, uint32_t colour);
static char input_path[4096];
static char renderer_a, renderer_b, game_texture;
static void *fake_library;
static struct link_map *fake_map;

static void check(int passed, const char *name)
{
  tests++;
  if (!passed) {
    failures++;
  }
  printf("%s %d - %s\n", passed ? "ok" : "not ok", tests, name);
}

static int rect_is(fake_rect rect, int x, int y, int w, int h)
{
  return rect.x == x && rect.y == y && rect.w == w && rect.h == h;
}

static int file_is(const char *path, const char *expected)
{
  char line[256] = "";
  FILE *file = fopen(path, "r");

  if (file == NULL) {
    return 0;
  }
  if (fgets(line, sizeof line, file) == NULL) {
    line[0] = '\0';
  }
  fclose(file);
  line[strcspn(line, "\n")] = '\0';
  return strcmp(line, expected) == 0;
}

static void write_file(const char *path, const void *data, size_t size)
{
  FILE *file = fopen(path, "wb");

  if (file == NULL || fwrite(data, 1, size, file) != size) {
    perror(path);
    exit(2);
  }
  fclose(file);
}

static void make_dir(const char *path)
{
  if (mkdir(path, 0755) != 0) {
    perror(path);
    exit(2);
  }
}

static uint32_t crc32(const unsigned char *data, size_t size, uint32_t crc)
{
  size_t i;
  int bit;

  crc = ~crc;
  for (i = 0; i < size; i++) {
    crc ^= data[i];
    for (bit = 0; bit < 8; bit++) {
      crc = crc & 1 ? (crc >> 1) ^ 0xedb88320u : crc >> 1;
    }
  }
  return ~crc;
}

static unsigned char *put32(unsigned char *out, uint32_t value)
{
  out[0] = value >> 24;
  out[1] = value >> 16;
  out[2] = value >> 8;
  out[3] = value;
  return out + 4;
}

static unsigned char *put_chunk(unsigned char *out, const char *type, const unsigned char *data,
                                size_t size)
{
  unsigned char *start;

  out = put32(out, size);
  start = out;
  memcpy(out, type, 4);
  memcpy(out + 4, data, size);
  out += 4 + size;
  return put32(out, crc32(start, size + 4, 0));
}

/*
 * Writes an 8-bit RGBA PNG whose deflate stream is a single stored block, so
 * the fixtures need no zlib and every pixel is known to the byte.
 */
static void write_png(const char *name, int width, int height, const unsigned char *rgba)
{
  static unsigned char raw[1024], zlib[1100], png[1400];
  unsigned char header[13];
  size_t raw_size = 0, zlib_size = 0, png_size;
  uint32_t a = 1, b = 0;
  char path[8192];
  unsigned char *out;
  int row;
  size_t i;

  for (row = 0; row < height; row++) {
    raw[raw_size++] = 0;
    memcpy(raw + raw_size, rgba + row * width * 4, width * 4);
    raw_size += width * 4;
  }
  for (i = 0; i < raw_size; i++) {
    a = (a + raw[i]) % 65521;
    b = (b + a) % 65521;
  }

  zlib[zlib_size++] = 0x78;
  zlib[zlib_size++] = 0x01;
  zlib[zlib_size++] = 0x01;
  zlib[zlib_size++] = raw_size & 0xff;
  zlib[zlib_size++] = raw_size >> 8;
  zlib[zlib_size++] = ~raw_size & 0xff;
  zlib[zlib_size++] = (~raw_size >> 8) & 0xff;
  memcpy(zlib + zlib_size, raw, raw_size);
  zlib_size += raw_size;
  put32(zlib + zlib_size, (b << 16) | a);
  zlib_size += 4;

  put32(header, width);
  put32(header + 4, height);
  header[8] = 8;
  header[9] = 6;
  header[10] = header[11] = header[12] = 0;

  memcpy(png, "\x89PNG\r\n\x1a\n", 8);
  out = put_chunk(png + 8, "IHDR", header, sizeof header);
  out = put_chunk(out, "IDAT", zlib, zlib_size);
  out = put_chunk(out, "IEND", NULL, 0);
  png_size = out - png;

  snprintf(path, sizeof path, "%s/overlays/%s", dir, name);
  write_file(path, png, png_size);
}

static unsigned char wide_pixels[4 * 3 * 4];
static unsigned char square_pixels[2 * 2 * 4];

static void make_fixtures(void)
{
  static const char shortcuts[] =
      "screenshot menu+x\nreset menu+y\nscreen-mode l1\noverlay menu+r1\n";
  char path[8192];
  size_t i;

  if (mkdtemp(dir) == NULL) {
    perror("mkdtemp");
    exit(2);
  }
  snprintf(path, sizeof path, "%s/overlays", dir);
  make_dir(path);

  for (i = 0; i < sizeof wide_pixels; i++) {
    wide_pixels[i] = (unsigned char)(i * 7);
  }
  for (i = 0; i < sizeof square_pixels; i++) {
    square_pixels[i] = (unsigned char)(200 - i);
  }

  /* listed in name order as a.png, b.png, z.png */
  write_png("a.png", 4, 3, wide_pixels);
  write_png("b.png", 2, 2, square_pixels);
  snprintf(path, sizeof path, "%s/overlays/z.png", dir);
  write_file(path, "not a png", 9);

  /* neither may become a choice */
  snprintf(path, sizeof path, "%s/overlays/._a.png", dir);
  write_file(path, "macOS metadata", 14);
  snprintf(path, sizeof path, "%s/overlays/notes.txt", dir);
  write_file(path, "notes", 5);

  snprintf(choice_path, sizeof choice_path, "%s/overlay", dir);
  write_file(choice_path, "b.png\n", 6);
  snprintf(screen_mode_path, sizeof screen_mode_path, "%s/screen-mode", dir);
  write_file(screen_mode_path, "standard\n", 9);

  snprintf(copy_carts_dir, sizeof copy_carts_dir, "%s/userdata", dir);
  make_dir(copy_carts_dir);
  snprintf(path, sizeof path, "%s/shared", dir);
  make_dir(path);
  setenv("PICO_PAK_SHARED_COPY_CARTS_DIR", path, 1);
  snprintf(pico8_config_path, sizeof pico8_config_path, "%s/pico8-config", dir);
  snprintf(path, sizeof path, "%s/config.txt", dir);
  write_file(path, "volume 256\n", 11);
  setenv("PICO_PAK_INSTALLED_CONFIG", path, 1);

  snprintf(path, sizeof path, "%s/input", dir);
  make_dir(path);
  setenv("PICO_PAK_INPUT_DIR", path, 1);
  snprintf(input_path, sizeof input_path, "%s/input/event0", dir);
  write_file(input_path, "", 0);

  snprintf(path, sizeof path, "%s/power", dir);
  make_dir(path);
  setenv("PICO_PAK_POWER_SUPPLY_DIR", path, 1);
  snprintf(path, sizeof path, "%s/power/battery", dir);
  make_dir(path);
  snprintf(path, sizeof path, "%s/power/battery/type", dir);
  write_file(path, "Battery\n", 8);
  snprintf(path, sizeof path, "%s/power/battery/capacity", dir);
  write_file(path, "50\n", 3);

  /* keymon's shared settings, and the fake standing in for its library */
  snprintf(path, sizeof path, "%s/SharedSettings", dir);
  write_file(path, "", 0);
  setenv("PICO_PAK_SHARED_SETTINGS", path, 1);
  /* the executable has its own copy of the fake's symbols, so the loader names the library */
  fake_library = dlopen("libSDL2-2.0.so.0", RTLD_NOW | RTLD_NOLOAD);
  if (fake_library == NULL || dlinfo(fake_library, RTLD_DI_LINKMAP, &fake_map) != 0 ||
      fake_map->l_name == NULL || fake_map->l_name[0] != '/') {
    printf("Bail out! cannot find the fake libSDL2's path\n");
    exit(2);
  }
  setenv("PICO_PAK_MSETTINGS", fake_map->l_name, 1);
  fake.volume = 10;
  fake.brightness = 5;

  snprintf(path, sizeof path, "%s/overlays", dir);
  setenv("PICO_PAK_OVERLAY_DIR", path, 1);
  setenv("PICO_PAK_OVERLAY_CHOICE", choice_path, 1);
  setenv("PICO_PAK_SCREEN_MODE", screen_mode_path, 1);
  setenv("PICO_PAK_COPY_CARTS_DIR", copy_carts_dir, 1);
  setenv("PICO_PAK_PICO8_CONFIG", pico8_config_path, 1);
  snprintf(splore_artwork_path, sizeof splore_artwork_path, "%s/splore-artwork", dir);
  setenv("PICO_PAK_SPLORE_ARTWORK", splore_artwork_path, 1);
  /* a cart that is its own label, and NextUI's switcher folder, not there yet */
  snprintf(path, sizeof path, "%s/Celeste.p8", dir);
  write_file(path, "\x89PNG\r\n\x1a\nlabel", 13);
  setenv("PICO_PAK_CART_PATH", path, 1);
  snprintf(switcher_dir, sizeof switcher_dir, "%s/.minui/PICO", dir);
  setenv("PICO_PAK_SWITCHER_DIR", switcher_dir, 1);

  /* MENU+X screenshot, MENU+Y reset, L1 alone screen mode, MENU+R1 overlay */
  snprintf(shortcuts_path, sizeof shortcuts_path, "%s/shortcuts", dir);
  write_file(shortcuts_path, shortcuts, strlen(shortcuts));
  setenv("PICO_PAK_SHORTCUTS", shortcuts_path, 1);
  snprintf(screenshots_dir, sizeof screenshots_dir, "%s/Screenshots", dir);
  setenv("PICO_PAK_SCREENSHOTS_DIR", screenshots_dir, 1);
  unsetenv("PICO_PAK_NOTIFY_SCREENSHOT");
  /* no font, so the menu draws with its built-in one */
  unsetenv("PICO_PAK_MENU_FONT");
}

static void release_all(void)
{
  memset(fake.buttons, 0, sizeof fake.buttons);
  memset(fake.raw_buttons, 0, sizeof fake.raw_buttons);
}

/* presses a button with Select held, as pico-8 would see it read that frame */
static uint8_t combo(void *pad, int button)
{
  fake.buttons[BUTTON_BACK] = 1;
  fake.buttons[button] = 1;
  return SDL_GameControllerGetButton(pad, button);
}

/* presses and releases Menu with the frames pico-8 reads the pad in between */
static void tap_menu(void *pad)
{
  fake.buttons[BUTTON_GUIDE] = 1;
  SDL_GameControllerGetButton(pad, BUTTON_GUIDE);
  fake.buttons[BUTTON_GUIDE] = 0;
  SDL_GameControllerGetButton(pad, BUTTON_A);
}

static void set_script(const uint32_t *states, int count)
{
  if (count > 0) {
    memcpy(fake.script, states, count * sizeof *states);
  }
  fake.script_length = count;
  fake.delays = 0;
}

/*
 * Taps Menu, opens Options and steps the option on row by steps, then backs
 * out with B twice. Every press is followed by a release, as a thumb would.
 */
static void change_option(void *pad, void *renderer, int row, int steps)
{
  uint32_t states[32];
  int count = 0, i;

  states[count++] = BIT(BUTTON_DPAD_DOWN);
  states[count++] = 0;
  states[count++] = BIT(BUTTON_A);
  states[count++] = 0;
  for (i = 0; i < row; i++) {
    states[count++] = BIT(BUTTON_DPAD_DOWN);
    states[count++] = 0;
  }
  for (i = 0; i < (steps < 0 ? -steps : steps); i++) {
    states[count++] = BIT(steps < 0 ? BUTTON_DPAD_LEFT : BUTTON_DPAD_RIGHT);
    states[count++] = 0;
  }
  states[count++] = BIT(BUTTON_B);
  states[count++] = 0;
  states[count++] = BIT(BUTTON_B);

  release_all();
  tap_menu(pad);
  set_script(states, count);
  fake.copies = 0;
  SDL_RenderPresent(renderer);
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_B);
}

static void test_select_combos(void *pad)
{
  check(combo(pad, BUTTON_DPAD_RIGHT) == 1 && file_is(choice_path, "b.png"),
        "Select + Right reaches pico-8 and changes no overlay");
  release_all();
  check(combo(pad, BUTTON_DPAD_DOWN) == 1 && file_is(screen_mode_path, "standard"),
        "Select + Down reaches pico-8 and changes no screen mode");
  release_all();
  check(combo(pad, BUTTON_START) == 1 && fake.pushed == 0, "Select + Start reaches pico-8");
  release_all();
  check(combo(pad, BUTTON_GUIDE) == 0 && fake.pushed == 0,
        "Select + Menu is hidden from pico-8 and does not quit");
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);
}

static void test_overlays(void *pad)
{
  struct fake_copy *copy;
  char path[8192];
  int copies, surfaces;

  snprintf(path, sizeof path, "%s/Celeste.p8.0.bmp", switcher_dir);
  check(file_is(path, "\x89PNG\r"), "the cart's artwork is the switcher's preview from the start");
  snprintf(path, sizeof path, "%s/Celeste.p8.txt", switcher_dir);
  check(file_is(path, "0"), "with slot 0 named for it");

  /* pico-8 letterboxes with every one of these, and the overlay must not */
  fake.logical_w = fake.logical_h = 128;
  fake.scale_x = fake.scale_y = 2.0f;
  fake.viewport = (fake_rect){128, 0, 768, 768};
  fake.clip_enabled = 1;
  fake.clip = (fake_rect){0, 0, 128, 128};

  SDL_RenderPresent(&renderer_a);
  check(fake.presents == 1, "the frame still reaches SDL_RenderPresent");
  check(fake.surfaces == 1 && fake.surface_w == 2 && fake.surface_h == 2,
        "the saved choice b.png is loaded on the first frame");
  check(fake.surface_depth == 32 && fake.surface_pitch == 8, "the surface is 32-bit RGBA rows");
  check(fake.surface_masks[0] == 0x000000ff && fake.surface_masks[1] == 0x0000ff00 &&
            fake.surface_masks[2] == 0x00ff0000 && fake.surface_masks[3] == 0xff000000,
        "the masks match stb_image's byte order");
  check(memcmp(fake.surface_pixels, square_pixels, sizeof square_pixels) == 0,
        "the PNG decodes to its pixels");
  check(fake.textures_live == 1, "the surface becomes one texture");

  copy = &fake.copy[0];
  check(fake.copies == 1 && copy->has_dst && rect_is(copy->dst, 128, 0, 768, 768),
        "a square overlay is fitted and centred on a 4:3 screen");
  check(copy->logical_w == 0 && copy->logical_h == 0 && copy->scale_x == 1.0f &&
            copy->scale_y == 1.0f &&
            rect_is(copy->viewport, 0, 0, FAKE_OUTPUT_W, FAKE_OUTPUT_H) && !copy->clip_enabled,
        "the overlay is copied with the letterboxing cleared");
  check(fake.logical_w == 128 && fake.logical_h == 128 && fake.scale_x == 2.0f &&
            fake.scale_y == 2.0f && rect_is(fake.viewport, 128, 0, 768, 768) &&
            fake.clip_enabled && rect_is(fake.clip, 0, 0, 128, 128),
        "pico-8's letterboxing is put back after the copy");

  SDL_RenderPresent(&renderer_a);
  check(fake.surfaces == 1 && fake.copies == 2, "the texture is reused on the next frame");

  change_option(pad, &renderer_a, ROW_OVERLAY, 1);
  check(file_is(choice_path, "z.png"), "stepping Overlay right moves to z.png and saves it");
  copies = fake.copies;
  SDL_RenderPresent(&renderer_a);
  check(fake.copies == copies && fake.textures_live == 0,
        "an unreadable PNG draws nothing and frees the old texture");
  SDL_RenderPresent(&renderer_a);
  check(fake.copies == copies && fake.surfaces == 1, "an unreadable PNG is not retried every frame");

  change_option(pad, &renderer_a, ROW_OVERLAY, 1);
  check(file_is(choice_path, "disabled"), "the next step right disables the overlay");
  copies = fake.copies;
  SDL_RenderPresent(&renderer_a);
  check(fake.copies == copies, "nothing is drawn while disabled");

  change_option(pad, &renderer_a, ROW_OVERLAY, 1);
  check(file_is(choice_path, "a.png"),
        "the overlays wrap around to a.png, skipping ._ files and other types");
  copies = fake.copies;
  SDL_RenderPresent(&renderer_a);
  copy = &fake.copy[fake.copies - 1];
  check(fake.surfaces == 2 && fake.surface_w == 4 && fake.surface_h == 3 &&
            memcmp(fake.surface_pixels, wide_pixels, sizeof wide_pixels) == 0,
        "a.png is decoded to its pixels");
  check(fake.copies == copies + 1 && rect_is(copy->dst, 0, 0, FAKE_OUTPUT_W, FAKE_OUTPUT_H),
        "a 4:3 overlay fills a 4:3 screen");

  change_option(pad, &renderer_a, ROW_OVERLAY, -1);
  check(file_is(choice_path, "disabled"), "stepping left from the first overlay disables it");
  change_option(pad, &renderer_a, ROW_OVERLAY, 1);
  check(file_is(choice_path, "a.png"), "stepping right comes back to it");

  SDL_DestroyRenderer(&renderer_a);
  copies = fake.copies;
  surfaces = fake.surfaces;
  SDL_RenderPresent(&renderer_b);
  check(fake.surfaces == surfaces + 1 && fake.copies == copies + 1,
        "a new renderer gets the overlay loaded again");
}

static void test_menu(void *pad)
{
  static const uint32_t press_a[] = {BIT(BUTTON_A)};
  uint32_t frame = 0x00ff8040;
  int presents, copies;

  fake.frame_colour = frame;
  fake.reads = fake.updates = fake.audio_pauses = fake.audio_resumes = 0;
  fake.streaming_textures = 0;

  fake.buttons[BUTTON_GUIDE] = 1;
  check(SDL_GameControllerGetButton(pad, BUTTON_GUIDE) == 0, "Menu is hidden from pico-8");
  fake.buttons[BUTTON_GUIDE] = 0;
  SDL_GameControllerGetButton(pad, BUTTON_A);
  check(fake.reads == 0, "releasing Menu opens nothing until the frame is presented");

  set_script(press_a, 1);
  presents = fake.presents;
  fake.copies = 0;
  SDL_RenderPresent(&renderer_b);
  check(fake.reads == 1 && fake.read_format == SDL_PIXELFORMAT_ARGB8888 &&
            fake.read_pitch == FAKE_OUTPUT_W * 4,
        "a Menu tap reads the frame back as ARGB8888");
  check(fake.read_state.logical_w == 0 && fake.read_state.scale_x == 1.0f &&
            rect_is(fake.read_state.viewport, 0, 0, FAKE_OUTPUT_W, FAKE_OUTPUT_H) &&
            !fake.read_state.clip_enabled,
        "the frame is read with the letterboxing cleared");
  check(fake.copies_at_read == 0 && fake.copies > 1,
        "the frame is read before the overlay is drawn, which is drawn after the menu");
  check(fake.updated[0] == 0xff7f3f1f, "the menu is drawn over the frame dimmed by half");
  check(fake.updates >= 2 && fake.updated[fake.updates - 1] == (frame | 0xff000000u),
        "the undimmed frame is put back when the menu closes");
  check(fake.presents == presents + 2,
        "the menu is presented while open and the frame once after");
  check(fake.delays == 1, "A on Continue closes the menu");
  check(fake.audio_pauses == 1 && fake.audio_resumes == 1,
        "pico-8's audio is paused while the menu is open");
  check(fake.logical_w == 128 && fake.scale_x == 2.0f && rect_is(fake.viewport, 128, 0, 768, 768) &&
            fake.clip_enabled,
        "pico-8's letterboxing is put back after the menu");
  check(fake.streaming_textures == 1 && fake.textures_live == 1,
        "the menu's texture is freed when it closes");

  check(SDL_GameControllerGetButton(pad, BUTTON_A) == 0,
        "the A that closed the menu is hidden from pico-8 while held");
  fake.buttons[BUTTON_A] = 0;
  SDL_GameControllerGetButton(pad, BUTTON_A);
  fake.buttons[BUTTON_A] = 1;
  check(SDL_GameControllerGetButton(pad, BUTTON_A) == 1, "the next press of A reaches pico-8");
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);

  copies = fake.reads;
  SDL_RenderPresent(&renderer_b);
  check(fake.reads == copies, "the menu does not open again by itself");

  fake.audio_status = 0;
  tap_menu(pad);
  set_script(press_a, 1);
  SDL_RenderPresent(&renderer_b);
  check(fake.audio_pauses == 1 && fake.audio_resumes == 1,
        "audio pico-8 paused itself is left paused");
  fake.audio_status = 1;
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);
}

static void test_menu_closing(void *pad)
{
  static const uint32_t press_b[] = {BIT(BUTTON_B)};
  static const uint32_t tap[] = {0, BIT(BUTTON_GUIDE), 0};
  static const uint32_t brightness[] = {0, BIT(BUTTON_GUIDE), BIT(BUTTON_GUIDE), 0, BIT(BUTTON_B)};
  int reads;

  tap_menu(pad);
  set_script(press_b, 1);
  SDL_RenderPresent(&renderer_b);
  check(fake.delays == 1 && fake.pushed == 0, "B closes the menu");
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);

  tap_menu(pad);
  set_script(tap, 3);
  SDL_RenderPresent(&renderer_b);
  check(fake.delays == 3 && fake.pushed == 0, "a tap of Menu closes the menu once it is let go");
  reads = fake.reads;
  SDL_GameControllerGetButton(pad, BUTTON_A);
  SDL_RenderPresent(&renderer_b);
  check(fake.reads == reads, "the Menu that closed the menu does not open it again");

  /* the volume key lands while Menu is held, between the second and third delay */
  tap_menu(pad);
  set_script(brightness, 5);
  fake.raw_buttons[17] = 1;
  SDL_RenderPresent(&renderer_b);
  check(fake.delays == 5, "Menu held with another button does not close the menu");
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);
}

/*
 * Quitting leaves every frame black until pico-8 exits, so it runs last.
 */
static void test_quit(void *pad)
{
  static const uint32_t quit[] = {BIT(BUTTON_DPAD_DOWN), 0, BIT(BUTTON_DPAD_DOWN), 0,
                                  BIT(BUTTON_A)};
  int resumes = fake.audio_resumes, pushed = fake.pushed, copies, black_clears;

  fake.updates = 0;
  tap_menu(pad);
  set_script(quit, 5);
  SDL_RenderPresent(&renderer_b);
  check(fake.pushed == pushed + 1 && fake.pushed_type == SDL_QUIT,
        "Quit, past the unavailable Save and Load, pushes SDL_QUIT");
  check(fake.delays == 5 + 8 && fake.updates >= 9 &&
            fake.updated[fake.updates - 1] == 0xff000000u &&
            fake.updated[fake.updates - 2] != 0xff000000u,
        "Quit fades the menu out to black");
  check(fake.audio_resumes == resumes, "pico-8's audio stays paused after Quit");
  check(preview_is_frame(341, 256, 0x00ff8040),
        "Quit gives the switcher the game's last frame as its preview");

  copies = fake.copies;
  black_clears = fake.black_clears;
  fake.draw_colour[0] = 0x12;
  SDL_RenderCopy(&renderer_b, &game_texture, NULL, &(fake_rect){128, 0, 768, 768});
  SDL_RenderPresent(&renderer_b);
  check(fake.black_clears == black_clears + 1 && fake.copies == copies + 1,
        "every frame pico-8 presents after Quit is cleared to black");
  check(fake.draw_colour[0] == 0x12, "pico-8's draw colour is put back after the black");
  fake.draw_colour[0] = 0;
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);
}

/*
 * An SDL_QUIT arriving while the menu is open quits as Quit does. Quitting
 * cannot be undone, so this runs in a process of its own.
 */
static void test_quit_shortcut(void *pad)
{
  write_file(shortcuts_path, "quit menu+b\n", 12);
  fake.frame_colour = 0x00ff8040;
  fake.updates = 0;
  fake.buttons[BUTTON_GUIDE] = 1;
  fake.buttons[BUTTON_B] = 1;
  SDL_GameControllerGetButton(pad, BUTTON_A);
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);
  set_script(NULL, 0);
  SDL_RenderPresent(&renderer_a);

  check(fake.pushed == 1 && fake.pushed_type == SDL_QUIT, "the Quit shortcut quits pico-8");
  /* the first step of eight darkens by 31 of 255, from the game itself */
  check(fake.reads == 1 && fake.delays == 8 && fake.updated[0] == 0xffe07038u &&
            fake.updated[fake.updates - 1] == 0xff000000u,
        "fading to black from the game, as no menu was drawn over it");
  check(preview_is_frame(341, 256, 0x00ff8040), "and gives the switcher the game's last frame");
}

static void test_quit_event(void *pad)
{
  fake.updates = 0;
  tap_menu(pad);
  memset(fake.queue, 0, sizeof fake.queue);
  fake.queue[0][0] = SDL_QUIT;
  fake.queued = 1;
  fake.queue_head = 0;
  set_script(NULL, 0);
  SDL_RenderPresent(&renderer_a);
  check(fake.pushed == 1 && fake.pushed_type == SDL_QUIT,
        "an SDL_QUIT while the menu is open quits pico-8");
  check(fake.delays == 8 && fake.updates >= 8 && fake.updated[fake.updates - 1] == 0xff000000u,
        "it fades out to black from the game, as the menu never drew");
  fake.queued = fake.queue_head = 0;
}

/* the preview is a 24-bit BMP of the frame, whose bottom left pixel is colour */
static int preview_is_frame(int w, int h, uint32_t colour)
{
  char path[8192];
  unsigned char header[57];
  FILE *file;
  int ok;

  snprintf(path, sizeof path, "%s/Celeste.p8.0.bmp", switcher_dir);
  file = fopen(path, "rb");
  if (file == NULL) {
    return 0;
  }
  ok = fread(header, 1, sizeof header, file) == sizeof header;
  fclose(file);

  return ok && header[0] == 'B' && header[1] == 'M' &&
         (header[18] | header[19] << 8) == w && (header[22] | header[23] << 8) == h &&
         header[54] == (colour & 0xff) && header[55] == ((colour >> 8) & 0xff) &&
         header[56] == ((colour >> 16) & 0xff);
}

static void append_volume_key(void)
{
  struct input_event key = {.type = EV_KEY, .code = KEY_VOLUMEUP, .value = 1};
  FILE *input = fopen(input_path, "ab");

  if (input == NULL || fwrite(&key, sizeof key, 1, input) != 1) {
    perror(input_path);
    exit(2);
  }
  fclose(input);
}

/*
 * The indicator is drawn over the game at minarch's place, a 372x90 pill 15
 * pixels in from the top right of the Brick's 1024x768 screen.
 */
static int indicator_drawn(int copies)
{
  int i;

  for (i = copies; i < fake.copies; i++) {
    if (fake.copy[i].has_dst && rect_is(fake.copy[i].dst, 1024 - 15 - 372, 15, 372, 90)) {
      return 1;
    }
  }
  return 0;
}

static void test_indicator(void)
{
  int copies, updates;

  check(fake.settings_inits == 1, "the launcher's settings are opened once");

  copies = fake.copies;
  SDL_RenderPresent(&renderer_b);
  check(!indicator_drawn(copies), "nothing is shown while volume and brightness stay put");

  fake.volume = 12;
  copies = fake.copies;
  updates = fake.updates;
  SDL_RenderPresent(&renderer_b);
  check(indicator_drawn(copies) && fake.updates == updates + 1,
        "a change of volume shows the indicator over the game");
  check(fake.copy[fake.copies - 1].clip_enabled == 0 && fake.copy[fake.copies - 1].logical_w == 0,
        "the indicator is drawn with the letterboxing cleared");

  fake.ticks += 400;
  copies = fake.copies;
  updates = fake.updates;
  SDL_RenderPresent(&renderer_b);
  check(indicator_drawn(copies) && fake.updates == updates,
        "it stays a while, without being drawn again");

  fake.ticks += 200;
  copies = fake.copies;
  SDL_RenderPresent(&renderer_b);
  check(!indicator_drawn(copies), "and goes 500ms after the last change");

  fake.brightness = 6;
  copies = fake.copies;
  updates = fake.updates;
  SDL_RenderPresent(&renderer_b);
  check(indicator_drawn(copies) && fake.updates == updates + 1,
        "a change of brightness shows it too");
  fake.ticks += 600;
  SDL_RenderPresent(&renderer_b);

  append_volume_key();
  copies = fake.copies;
  SDL_RenderPresent(&renderer_b);
  check(indicator_drawn(copies), "a volume key with the volume at its end still shows it");
  fake.ticks += 600;
  SDL_RenderPresent(&renderer_b);
}

static void test_chords(void *pad)
{
  int reads = fake.reads;

  fake.buttons[BUTTON_GUIDE] = 1;
  SDL_GameControllerGetButton(pad, BUTTON_A);
  fake.raw_buttons[17] = 1;
  SDL_GameControllerGetButton(pad, BUTTON_A);
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);
  SDL_RenderPresent(&renderer_b);
  check(fake.reads == reads, "Menu held with a button no mapping names does not open the menu");

  fake.buttons[BUTTON_GUIDE] = 1;
  SDL_GameControllerGetButton(pad, BUTTON_A);
  append_volume_key();
  SDL_GameControllerGetButton(pad, BUTTON_A);
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);
  SDL_RenderPresent(&renderer_b);
  check(fake.reads == reads, "Menu held with a volume key does not open the menu");

  tap_menu(pad);
  set_script((const uint32_t[]){BIT(BUTTON_B)}, 1);
  SDL_RenderPresent(&renderer_b);
  check(fake.reads == reads + 1, "the next tap of Menu on its own opens the menu");
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);

  /* the volume key also showed the indicator, which has to be gone for the tests after */
  fake.ticks += 1000;
  SDL_RenderPresent(&renderer_b);
}

static void test_events(void *pad)
{
  uint32_t event[14];

  memset(fake.queue, 0, sizeof fake.queue);
  fake.queue[0][0] = SDL_CONTROLLERBUTTONDOWN;
  ((uint8_t *)&fake.queue[0][3])[0] = BUTTON_GUIDE;
  fake.queue[1][0] = SDL_KEYDOWN;
  fake.queue[2][0] = SDL_CONTROLLERBUTTONDOWN;
  ((uint8_t *)&fake.queue[2][3])[0] = BUTTON_DPAD_LEFT;
  fake.queued = 2;
  fake.queue_head = 0;

  fake.buttons[BUTTON_BACK] = 1;
  check(SDL_PollEvent(event) == 1 && event[0] == SDL_KEYDOWN, "a Menu event is swallowed");
  check(SDL_PollEvent(event) == 0, "nothing else is left in the queue");

  fake.queued = 3;
  fake.queue_head = 2;
  check(SDL_PollEvent(event) == 1 && event[0] == SDL_CONTROLLERBUTTONDOWN,
        "a d-pad event with Select held reaches pico-8");
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);
  fake.queued = fake.queue_head = 0;
}

static void test_screen_mode(void *pad)
{
  fake_rect game = {128, 0, 768, 768};
  fake_rect sprite = {200, 100, 10, 10};
  struct fake_copy *copy;
  int copies;

  SDL_RenderCopy(&renderer_b, &game_texture, NULL, &game);
  copy = &fake.copy[fake.copies - 1];
  check(rect_is(copy->dst, 128, 0, 768, 768), "standard mode leaves pico-8's copies alone");

  change_option(pad, &renderer_b, ROW_SCREEN_MODE, 1);
  check(file_is(screen_mode_path, "stretched"), "stepping Screen Mode right saves stretched");

  SDL_RenderCopy(&renderer_b, &game_texture, NULL, &game);
  copy = &fake.copy[fake.copies - 1];
  check(rect_is(copy->dst, 0, 0, FAKE_OUTPUT_W, FAKE_OUTPUT_H),
        "stretched mode maps pico-8's screen onto the whole output");
  SDL_RenderCopy(&renderer_b, &game_texture, NULL, &sprite);
  copy = &fake.copy[fake.copies - 1];
  check(rect_is(copy->dst, 96, 100, 13, 10), "stretched mode scales smaller copies with it");

  copies = fake.copies;
  SDL_RenderPresent(&renderer_b);
  check(fake.copies == copies, "no overlay is drawn in stretched mode");

  change_option(pad, &renderer_b, ROW_SCREEN_MODE, -1);
  check(file_is(screen_mode_path, "standard"), "stepping it back saves standard");
  SDL_RenderCopy(&renderer_b, &game_texture, NULL, &game);
  copy = &fake.copy[fake.copies - 1];
  check(rect_is(copy->dst, 128, 0, 768, 768), "standard mode stops stretching");
}

static void test_settings(void *pad)
{
  char path[8192];

  change_option(pad, &renderer_b, ROW_COPY_CARTS, 1);
  snprintf(path, sizeof path, "%s/copy-carts", copy_carts_dir);
  check(access(path, F_OK) == 0, "Copy Splore Carts writes its marker to the userdata folder");

  change_option(pad, &renderer_b, ROW_SPLORE_ARTWORK, 1);
  check(file_is(splore_artwork_path, "fancy"), "Splore Artwork saves the fancy cover");

  change_option(pad, &renderer_b, ROW_VOLUME, -1);
  check(file_is(pico8_config_path, "volume 192"),
        "Volume steps down from the installed config.txt's value and saves it");
}

#define BUTTON_X 2
#define BUTTON_Y 3
#define BUTTON_L1 9
#define BUTTON_R1 10
#define SDL_KEYUP 0x301
#define KMOD_LCTRL 0x0040

/* the PNG the screenshot shortcut saved, waiting for its thread to finish */
static int find_screenshot(char *found, size_t size)
{
  struct dirent *entry;
  DIR *folder;
  int tries;

  for (tries = 0; tries < 200; tries++) {
    folder = opendir(screenshots_dir);
    while (folder != NULL && (entry = readdir(folder)) != NULL) {
      size_t length = strlen(entry->d_name);
      if (length > 4 && strcmp(entry->d_name + length - 4, ".png") == 0) {
        int fits = snprintf(found, size, "%s/%s", screenshots_dir, entry->d_name) < (int)size;
        closedir(folder);
        return fits;
      }
    }
    if (folder != NULL) {
      closedir(folder);
    }
    usleep(10000);
  }
  return 0;
}

static void test_shortcuts(void *pad)
{
  char found[8192];
  unsigned char png[24];
  FILE *file;
  int pushed, reads, copies, i, notified = 0;

  /* L1 alone switches the screen mode, and never reaches pico-8 */
  release_all();
  fake.buttons[BUTTON_L1] = 1;
  check(SDL_GameControllerGetButton(pad, BUTTON_L1) == 0, "a button bound alone is hidden from pico-8");
  SDL_RenderPresent(&renderer_b);
  check(file_is(screen_mode_path, "stretched"), "and pressing it does its shortcut");
  SDL_RenderPresent(&renderer_b);
  check(file_is(screen_mode_path, "stretched"), "once a press");
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);
  fake.buttons[BUTTON_L1] = 1;
  SDL_GameControllerGetButton(pad, BUTTON_L1);
  SDL_RenderPresent(&renderer_b);
  check(file_is(screen_mode_path, "standard"), "and again the next");
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);

  /* X alone reaches pico-8, and MENU+X takes a screenshot */
  fake.buttons[BUTTON_X] = 1;
  check(SDL_GameControllerGetButton(pad, BUTTON_X) == 1,
        "a button bound with Menu still reaches pico-8 on its own");
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);

  reads = fake.reads;
  fake.buttons[BUTTON_GUIDE] = 1;
  SDL_GameControllerGetButton(pad, BUTTON_A);
  fake.buttons[BUTTON_X] = 1;
  check(SDL_GameControllerGetButton(pad, BUTTON_X) == 0, "but not with Menu held");
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);
  copies = fake.copies;
  SDL_RenderPresent(&renderer_b);
  check(fake.reads == reads + 1, "MENU+X reads the screen for a screenshot, and opens no menu");
  check(find_screenshot(found, sizeof found) && strstr(found, "/Celeste.") != NULL,
        "the screenshot is saved to the screenshots folder, named for the game");
  file = fopen(found, "rb");
  check(file != NULL && fread(png, 1, sizeof png, file) == sizeof png &&
            memcmp(png, "\x89PNG", 4) == 0 && png[18] == (1024 >> 8) && png[22] == (768 >> 8),
        "as a PNG of the whole screen");
  if (file != NULL) {
    fclose(file);
  }
  for (i = copies; i < fake.copies; i++) {
    notified |= fake.copy[i].has_dst && fake.copy[i].dst.h == 90 &&
                fake.copy[i].dst.x + fake.copy[i].dst.w == 1024 - 15 && fake.copy[i].dst.y == 15;
  }
  check(notified, "with a notice at the top right that it was saved");

  /* MENU+R1 steps through the overlays */
  fake.buttons[BUTTON_GUIDE] = 1;
  fake.buttons[BUTTON_R1] = 1;
  SDL_GameControllerGetButton(pad, BUTTON_A);
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);
  SDL_RenderPresent(&renderer_b);
  check(file_is(choice_path, "b.png"), "MENU+R1 moves to the next overlay and saves it");

  /* MENU+Y presses Ctrl+R for pico-8 */
  pushed = fake.pushed;
  fake.buttons[BUTTON_GUIDE] = 1;
  fake.buttons[BUTTON_Y] = 1;
  SDL_GameControllerGetButton(pad, BUTTON_A);
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);
  SDL_RenderPresent(&renderer_b);
  check(fake.pushed == pushed + 4 && fake.pushed_events[pushed][0] == SDL_KEYDOWN &&
            fake.pushed_events[pushed][4] == 224 && fake.pushed_events[pushed + 1][4] == 21 &&
            fake.pushed_events[pushed + 2][0] == SDL_KEYUP &&
            fake.pushed_events[pushed + 3][0] == SDL_KEYUP && fake.pushed_events[pushed + 3][4] == 224,
        "MENU+Y presses Ctrl, then R, and lets both go");
  check((fake.pushed_events[pushed + 1][6] & 0xffff) == KMOD_LCTRL,
        "with Ctrl held on the R");
  check(SDL_GetModState() == KMOD_LCTRL, "and Ctrl reported held to SDL_GetModState");

  memset(fake.queue, 0, sizeof fake.queue);
  fake.queue[0][0] = SDL_KEYUP;
  fake.queue[0][4] = 224;
  fake.queued = 1;
  fake.queue_head = 0;
  {
    uint32_t event[14];
    SDL_PollEvent(event);
  }
  check(SDL_GetModState() == 0, "until pico-8 reads Ctrl being let go");
  fake.queued = fake.queue_head = 0;
}

/*
 * Binding in the menu: Options, up to its last row, Shortcuts, down to Quit,
 * A, then Menu held with B pressed. B backs out of each page after.
 */
static void test_binding(void *pad)
{
  static const uint32_t states[] = {
      BIT(BUTTON_DPAD_DOWN), 0, BIT(BUTTON_A), 0, BIT(BUTTON_DPAD_UP), 0, BIT(BUTTON_A), 0,
      BIT(BUTTON_DPAD_DOWN), 0, BIT(BUTTON_A), 0, BIT(BUTTON_GUIDE),
      BIT(BUTTON_GUIDE) | BIT(BUTTON_B), BIT(BUTTON_GUIDE), 0, BIT(BUTTON_B), 0, BIT(BUTTON_B), 0,
      BIT(BUTTON_B)};
  char line[256] = "";
  FILE *file;
  int found = 0;

  release_all();
  tap_menu(pad);
  set_script(states, (int)(sizeof states / sizeof *states));
  SDL_RenderPresent(&renderer_b);
  check(fake.delays == (int)(sizeof states / sizeof *states),
        "binding with Menu held neither closes the menu nor opens one");

  file = fopen(shortcuts_path, "r");
  while (file != NULL && fgets(line, sizeof line, file) != NULL) {
    found |= strcmp(line, "quit menu+b\n") == 0;
  }
  if (file != NULL) {
    fclose(file);
  }
  check(found, "Quit is bound to MENU+B in the shortcuts file");
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);
}

/* whether the file holds text anywhere */
static int file_has(const char *path, const char *text)
{
  static char contents[65536];
  FILE *file = fopen(path, "r");
  size_t got;

  if (file == NULL) {
    return 0;
  }
  got = fread(contents, 1, sizeof contents - 1, file);
  contents[got] = '\0';
  fclose(file);
  return strstr(contents, text) != NULL;
}

/*
 * A real bus error, the kind the device showed: reading a mapped file past its
 * end. A child takes it, so the crash ends that process alone, with its log
 * sent to a file.
 */
static void test_crash(void)
{
  char log_path[8192], mapped_path[8192];
  int status, fd;
  pid_t child;

  snprintf(log_path, sizeof log_path, "%s/crash.log", dir);
  snprintf(mapped_path, sizeof mapped_path, "%s/empty-mapping", dir);
  write_file(mapped_path, "", 0);

  fflush(stdout);
  child = fork();
  if (child == 0) {
    volatile const char *mapped;

    if (freopen(log_path, "w", stderr) == NULL) {
      _exit(2);
    }
    fd = open(mapped_path, O_RDONLY);
    mapped = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
      _exit(2);
    }
    (void)mapped[0];
    _exit(0);
  }

  check(child > 0 && waitpid(child, &status, 0) == child && WIFSIGNALED(status) &&
            WTERMSIG(status) == SIGBUS,
        "a bus error still ends pico-8 with SIGBUS, for launch.sh to see");
  check(file_has(log_path, "] sdl-display: pico-8 crashed with SIGBUS"),
        "the crash is logged with the signal");
  check(file_has(log_path, "the address is in ") && file_has(log_path, "/empty-mapping"),
        "along with the file the address was mapped from");
  check(file_has(log_path, "the instruction is in ") && file_has(log_path, "backtrace:"),
        "and where the instruction was, with a backtrace");
}

static void test_closed_pad(void *pad)
{
  SDL_GameControllerClose(pad);
  fake.buttons[BUTTON_GUIDE] = 1;
  check(SDL_GameControllerGetButton(pad, BUTTON_GUIDE) == 1,
        "a closed pad's Menu button reaches pico-8");
  release_all();
}

int main(int argc, char **argv)
{
  void *pad;

  make_fixtures();

  pad = SDL_GameControllerOpen(0);
  check(pad != NULL, "the pad pico-8 opens is passed through");
  check(SDL_OpenAudioDevice(NULL, 0, NULL, NULL, 0) == FAKE_AUDIO_DEVICE,
        "the audio device pico-8 opens is passed through");

  if (argc > 1 && strcmp(argv[1], "quit-event") == 0) {
    test_quit_event(pad);
    printf("1..%d\n", tests);
    return failures == 0 ? 0 : 1;
  }
  if (argc > 1 && strcmp(argv[1], "quit-shortcut") == 0) {
    test_quit_shortcut(pad);
    printf("1..%d\n", tests);
    return failures == 0 ? 0 : 1;
  }

  test_select_combos(pad);
  test_overlays(pad);
  test_menu(pad);
  test_menu_closing(pad);
  test_chords(pad);
  test_events(pad);
  test_screen_mode(pad);
  test_settings(pad);
  test_shortcuts(pad);
  test_binding(pad);
  test_indicator();
  test_quit(pad);
  test_crash();
  test_closed_pad(pad);

  printf("1..%d\n", tests);
  return failures == 0 ? 0 : 1;
}
