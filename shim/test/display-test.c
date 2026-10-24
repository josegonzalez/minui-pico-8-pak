/*
 * Plays pico-8's part against sdl-display.so, which is preloaded over the fake
 * libSDL2 in fake-sdl.c, and checks what reached the fake:
 *
 *   LD_PRELOAD=sdl-display.so display-test [quit-event | quit-shortcut | shaders]
 *   PICO_PAK_STATES_DIR=/tmp/display-test-states/states \
 *     LD_PRELOAD=sdl-display.so display-test states | resume | shutdown | quit
 *
 * The menu holds pico-8 inside SDL_RenderPresent, so each menu is driven by a
 * script of pad states the fake steps through on every SDL_Delay. The shim
 * reads its environment once, so the tests run in one process in order, apart
 * from quitting on an SDL_QUIT, which like any quit cannot be undone and so has
 * a run of its own. Prints TAP and exits non-zero on a failure.
 *
 * The save state runs play pico-8 for real: this program has pico-8's symbols,
 * so its own memory is what a state saves and puts back, and resume, in a
 * process of its own, puts back the state states left. The shim only takes a
 * program for pico-8 when the variable is set as it starts, so it is set on
 * the command line. The fake's state is linked into this program's memory,
 * so a load puts that back too, and the tests set what they need after one.
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
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
#define SDL_CONTROLLERAXISMOTION 0x650
#define SDL_CONTROLLERBUTTONDOWN 0x651
#define SDL_CONTROLLERBUTTONUP 0x652
#define AXIS_TRIGGERLEFT 4
#define AXIS_TRIGGERRIGHT 5
#define SDL_PIXELFORMAT_ARGB8888 0x16362004
#define SDL_TEXTUREACCESS_STREAMING 1
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

/* the menu's Options groups, in order, and the rows the tests change in each */
enum { GROUP_FRONTEND, GROUP_EMULATOR, GROUP_SHADERS, GROUP_SPLORE };
enum { ROW_SCREEN_MODE = 0, ROW_OVERLAY = 1 };
enum { ROW_VOLUME = 0 };
enum { ROW_COPY_CARTS = 0, ROW_SPLORE_ARTWORK = 1 };

/* what pico-8 calls, answered by the shim and passed on to the fake */
void SDL_RenderPresent(void *renderer);
void SDL_DestroyRenderer(void *renderer);
int SDL_PollEvent(void *event);
int SDL_RenderCopy(void *renderer, void *texture, const fake_rect *src, const fake_rect *dst);
void *SDL_GameControllerOpen(int index);
void SDL_GameControllerClose(void *controller);
uint8_t SDL_GameControllerGetButton(void *controller, int button);
int16_t SDL_GameControllerGetAxis(void *controller, int axis);
uint32_t SDL_OpenAudioDevice(const char *device, int iscapture, const void *desired,
                             void *obtained, int allowed_changes);
int SDL_GetModState(void);
void *SDL_CreateTexture(void *renderer, uint32_t format, int access, int w, int h);
int SDL_UpdateTexture(void *texture, const fake_rect *rect, const void *pixels, int pitch);

/*
 * The counts live in a mapping of their own, outside the memory a state puts
 * back, so a load never forgets a check made after the save
 */
static struct {
  int tests;
  int failures;
} *tally;
static char dir[] = "/tmp/display-test.XXXXXX";
/* the save state runs share one folder, as resume reads what states saved */
#define STATES_FIXTURES "/tmp/display-test-states"

/*
 * pico-8's own names, which the shim looks for to know it is in pico-8: its
 * machine, the cart's Lua state once the cart is running, and the downloads
 * it has going. The runs without states never look at them.
 */
char pstate[4096];
void *L0_cart;
int num_threads;

/* what the save state runs change between a save and a load, in pico-8's memory and its arena */
static int marker;
static char *heap;
/* a block as big as the data pico-8 loads from pico8.dat, allocated before the cart starts */
#define BIG_SIZE (2u << 20)
static unsigned char *big;
static char choice_path[4096];
static char screen_mode_path[4096];
static char copy_carts_dir[4096];
static char pico8_config_path[4096];
static char splore_artwork_path[4096];
static char switcher_dir[4096];
static char shortcuts_path[4096];
static char controls_path[4096];
static char screenshots_dir[4096];
static char auto_resume_path[4096];
static char auto_save_path[4096];

static int preview_is_frame(int slot, int w, int h, uint32_t colour);
static char input_path[4096];
static char renderer_a, renderer_b, game_texture;
static void *fake_library;
static struct link_map *fake_map;

static void check(int passed, const char *name)
{
  tally->tests++;
  if (!passed) {
    tally->failures++;
  }
  printf("%s %d - %s\n", passed ? "ok" : "not ok", tally->tests, name);
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

/* the save state runs make their folders again over the last run's */
static void make_dir(const char *path)
{
  if (mkdir(path, 0755) != 0 && errno != EEXIST) {
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

static void make_fixtures(int states)
{
  static const char shortcuts[] =
      "screenshot menu+x\nreset menu+y\nscreen-mode l1\noverlay menu+r2\nsave-gif r3\n"
      "cpu-meter l3\n";
  static const char controls[] = "up menu+r1\nx x\n";
  char path[8192];
  size_t i;

  if (states) {
    snprintf(dir, sizeof dir, "%s", STATES_FIXTURES);
    make_dir(dir);
  } else if (mkdtemp(dir) == NULL) {
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
  /* as under NextUI, whose switcher shows the artwork until there is a state */
  setenv("PICO_PAK_STATELESS_PREVIEWS", "1", 1);
  snprintf(auto_resume_path, sizeof auto_resume_path, "%s/auto_resume.txt", dir);
  setenv("PICO_PAK_AUTO_RESUME_PATH", auto_resume_path, 1);
  setenv("PICO_PAK_AUTO_RESUME_ROM", "/Roms/Pico-8 (PICO)/Celeste.p8", 1);
  snprintf(auto_save_path, sizeof auto_save_path, "%s/auto-save-on-exit", dir);
  setenv("PICO_PAK_AUTO_SAVE", auto_save_path, 1);

  /*
   * MENU+X screenshot, MENU+Y reset, L1 alone screen mode, MENU+R2 overlay,
   * R3 alone a GIF and L3 alone the CPU meter
   */
  snprintf(shortcuts_path, sizeof shortcuts_path, "%s/shortcuts", dir);
  write_file(shortcuts_path, shortcuts, strlen(shortcuts));
  setenv("PICO_PAK_SHORTCUTS", shortcuts_path, 1);
  /* PICO-8's Up on MENU+R1 and its X on the X button, the rest as they start */
  snprintf(controls_path, sizeof controls_path, "%s/controls", dir);
  write_file(controls_path, controls, strlen(controls));
  setenv("PICO_PAK_CONTROLS", controls_path, 1);
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
  memset(fake.axes, 0, sizeof fake.axes);
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
 * Taps Menu, opens Options and the group, steps the setting on row by steps,
 * then backs out with B three times. Every press is followed by a release, as
 * a thumb would.
 */
static void change_option(void *pad, void *renderer, int group, int row, int steps)
{
  uint32_t states[32];
  int count = 0, i;

  states[count++] = BIT(BUTTON_DPAD_DOWN);
  states[count++] = 0;
  states[count++] = BIT(BUTTON_A);
  states[count++] = 0;
  for (i = 0; i < group; i++) {
    states[count++] = BIT(BUTTON_DPAD_DOWN);
    states[count++] = 0;
  }
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

  change_option(pad, &renderer_a, GROUP_FRONTEND, ROW_OVERLAY, 1);
  check(file_is(choice_path, "z.png"), "stepping Overlay right moves to z.png and saves it");
  copies = fake.copies;
  SDL_RenderPresent(&renderer_a);
  check(fake.copies == copies && fake.textures_live == 0,
        "an unreadable PNG draws nothing and frees the old texture");
  SDL_RenderPresent(&renderer_a);
  check(fake.copies == copies && fake.surfaces == 1, "an unreadable PNG is not retried every frame");

  change_option(pad, &renderer_a, GROUP_FRONTEND, ROW_OVERLAY, 1);
  check(file_is(choice_path, "disabled"), "the next step right disables the overlay");
  copies = fake.copies;
  SDL_RenderPresent(&renderer_a);
  check(fake.copies == copies, "nothing is drawn while disabled");

  change_option(pad, &renderer_a, GROUP_FRONTEND, ROW_OVERLAY, 1);
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

  change_option(pad, &renderer_a, GROUP_FRONTEND, ROW_OVERLAY, -1);
  check(file_is(choice_path, "disabled"), "stepping left from the first overlay disables it");
  change_option(pad, &renderer_a, GROUP_FRONTEND, ROW_OVERLAY, 1);
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
  check(preview_is_frame(0, 341, 256, 0x00ff8040),
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
  check(preview_is_frame(0, 341, 256, 0x00ff8040), "and gives the switcher the game's last frame");
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

/* a slot's preview is a 24-bit BMP of the frame, whose bottom left pixel is colour */
static int preview_is_frame(int slot, int w, int h, uint32_t colour)
{
  char path[8192];
  unsigned char header[57];
  FILE *file;
  int ok;

  snprintf(path, sizeof path, "%s/Celeste.p8.%d.bmp", switcher_dir, slot);
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
  fake.queue[1][0] = SDL_CONTROLLERBUTTONUP;
  ((uint8_t *)&fake.queue[1][3])[0] = BUTTON_GUIDE;
  fake.queue[2][0] = SDL_KEYDOWN;
  fake.queue[3][0] = SDL_CONTROLLERBUTTONDOWN;
  ((uint8_t *)&fake.queue[3][3])[0] = BUTTON_DPAD_LEFT;
  fake.queued = 3;
  fake.queue_head = 0;

  fake.buttons[BUTTON_BACK] = 1;
  check(SDL_PollEvent(event) == 1 && event[0] == SDL_KEYDOWN,
        "Menu's press and release events are swallowed");
  check(SDL_PollEvent(event) == 0, "nothing else is left in the queue");

  fake.queued = 4;
  fake.queue_head = 3;
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

  change_option(pad, &renderer_b, GROUP_FRONTEND, ROW_SCREEN_MODE, 1);
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

  change_option(pad, &renderer_b, GROUP_FRONTEND, ROW_SCREEN_MODE, -1);
  check(file_is(screen_mode_path, "standard"), "stepping it back saves standard");
  SDL_RenderCopy(&renderer_b, &game_texture, NULL, &game);
  copy = &fake.copy[fake.copies - 1];
  check(rect_is(copy->dst, 128, 0, 768, 768), "standard mode stops stretching");
}

static void test_settings(void *pad)
{
  char path[8192];

  change_option(pad, &renderer_b, GROUP_SPLORE, ROW_COPY_CARTS, 1);
  snprintf(path, sizeof path, "%s/copy-carts", copy_carts_dir);
  check(access(path, F_OK) == 0, "Copy Splore Carts writes its marker to the userdata folder");

  change_option(pad, &renderer_b, GROUP_SPLORE, ROW_SPLORE_ARTWORK, 1);
  check(file_is(splore_artwork_path, "fancy"), "Splore Artwork saves the fancy cover");

  change_option(pad, &renderer_b, GROUP_EMULATOR, ROW_VOLUME, -1);
  check(file_is(pico8_config_path, "volume 192"),
        "Volume steps down from the installed config.txt's value and saves it");
}

#define BUTTON_X 2
#define BUTTON_Y 3
#define BUTTON_L3 7
#define BUTTON_R3 8
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

/* presses a button bound alone to a shortcut that presses Ctrl and scancode, and lets it all go */
static void test_ctrl_key(void *pad, int button, uint32_t scancode, const char *name)
{
  int pushed = fake.pushed, i;
  uint32_t event[14];

  release_all();
  fake.buttons[button] = 1;
  SDL_GameControllerGetButton(pad, BUTTON_A);
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);
  for (i = 0; i < 4; i++) {
    SDL_RenderPresent(&renderer_b);
    fake.ticks += 100;
  }
  check(fake.pushed == pushed + 4 && fake.pushed_events[pushed][4] == 224 &&
            fake.pushed_events[pushed + 1][0] == SDL_KEYDOWN &&
            fake.pushed_events[pushed + 1][4] == scancode &&
            fake.pushed_events[pushed + 2][0] == SDL_KEYUP &&
            fake.pushed_events[pushed + 2][4] == scancode &&
            fake.pushed_events[pushed + 3][0] == SDL_KEYUP &&
            fake.pushed_events[pushed + 3][4] == 224,
        name);

  memset(fake.queue, 0, sizeof fake.queue);
  fake.queue[0][0] = SDL_KEYUP;
  fake.queue[0][4] = 224;
  fake.queued = 1;
  fake.queue_head = 0;
  SDL_PollEvent(event);
  fake.queued = fake.queue_head = 0;
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

  /* X alone reaches pico-8 as its X, which pico-8 reads from B, and MENU+X takes a screenshot */
  fake.buttons[BUTTON_X] = 1;
  check(SDL_GameControllerGetButton(pad, BUTTON_B) == 1,
        "a button bound with Menu still reaches pico-8 on its own");
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);

  reads = fake.reads;
  fake.buttons[BUTTON_GUIDE] = 1;
  SDL_GameControllerGetButton(pad, BUTTON_A);
  fake.buttons[BUTTON_X] = 1;
  check(SDL_GameControllerGetButton(pad, BUTTON_B) == 0, "but not with Menu held");
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

  /* R2 is a trigger's axis, which reaches pico-8 on its own, and MENU+R2 steps through the overlays */
  fake.axes[AXIS_TRIGGERRIGHT] = 32767;
  check(SDL_GameControllerGetAxis(pad, AXIS_TRIGGERRIGHT) == 32767,
        "a trigger bound with Menu still reaches pico-8 on its own");
  SDL_RenderPresent(&renderer_b);
  check(file_is(choice_path, "a.png"), "and does nothing then");
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);

  fake.buttons[BUTTON_GUIDE] = 1;
  SDL_GameControllerGetButton(pad, BUTTON_A);
  fake.axes[AXIS_TRIGGERRIGHT] = 32767;
  check(SDL_GameControllerGetAxis(pad, AXIS_TRIGGERRIGHT) == 0, "but not with Menu held");
  fake.axes[AXIS_TRIGGERLEFT] = 20000;
  check(SDL_GameControllerGetAxis(pad, AXIS_TRIGGERLEFT) == 20000,
        "while the other trigger, bound to nothing, still reaches it");
  fake.axes[AXIS_TRIGGERLEFT] = 0;
  {
    uint32_t event[14];

    memset(fake.queue, 0, sizeof fake.queue);
    fake.queue[0][0] = SDL_CONTROLLERAXISMOTION;
    ((uint8_t *)&fake.queue[0][3])[0] = AXIS_TRIGGERRIGHT;
    fake.queued = 1;
    fake.queue_head = 0;
    check(SDL_PollEvent(event) == 0, "nor as an event");
    fake.queued = fake.queue_head = 0;
  }
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);
  SDL_RenderPresent(&renderer_b);
  check(file_is(choice_path, "b.png"),
        "MENU+R2 moves to the next overlay and saves it, without opening the menu");

  /* MENU+Y presses Ctrl+R for pico-8, a step every 100ms */
  pushed = fake.pushed;
  fake.buttons[BUTTON_GUIDE] = 1;
  fake.buttons[BUTTON_Y] = 1;
  SDL_GameControllerGetButton(pad, BUTTON_A);
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);
  SDL_RenderPresent(&renderer_b);
  check(fake.pushed == pushed + 1 && fake.pushed_events[pushed][0] == SDL_KEYDOWN &&
            fake.pushed_events[pushed][4] == 224,
        "MENU+Y presses Ctrl first");
  check(SDL_GetModState() == KMOD_LCTRL, "and Ctrl reported held to SDL_GetModState");
  SDL_RenderPresent(&renderer_b);
  check(fake.pushed == pushed + 1, "R waits, as pico-8 ignores an R straight after Ctrl");

  fake.ticks += 100;
  SDL_RenderPresent(&renderer_b);
  check(fake.pushed == pushed + 2 && fake.pushed_events[pushed + 1][0] == SDL_KEYDOWN &&
            fake.pushed_events[pushed + 1][4] == 21 &&
            (fake.pushed_events[pushed + 1][6] & 0xffff) == KMOD_LCTRL,
        "then R is pressed with Ctrl held");
  fake.ticks += 100;
  SDL_RenderPresent(&renderer_b);
  check(fake.pushed == pushed + 3 && fake.pushed_events[pushed + 2][0] == SDL_KEYUP &&
            fake.pushed_events[pushed + 2][4] == 21,
        "then let go");
  fake.ticks += 100;
  SDL_RenderPresent(&renderer_b);
  check(fake.pushed == pushed + 4 && fake.pushed_events[pushed + 3][0] == SDL_KEYUP &&
            fake.pushed_events[pushed + 3][4] == 224,
        "and then Ctrl is let go");
  fake.ticks += 100;
  SDL_RenderPresent(&renderer_b);
  check(fake.pushed == pushed + 4, "and nothing more is pressed");
  check(SDL_GetModState() == KMOD_LCTRL, "Ctrl is still reported held");

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

  /* R3 saves a GIF with Ctrl+9, into the screenshots folder, and L3 toggles the CPU meter */
  test_ctrl_key(pad, BUTTON_R3, 38, "R3 presses Ctrl+9 for a GIF");
  check(access(screenshots_dir, F_OK) == 0, "with the screenshots folder there for it");
  test_ctrl_key(pad, BUTTON_L3, 19, "L3 presses Ctrl+P for the CPU meter");
}

/*
 * Binding in the menu: Options, up to its last row, Shortcuts, down past
 * Save GIF to Quit, A, then Menu held with B pressed. B backs out of each
 * page after.
 */
static void test_binding(void *pad)
{
  static const uint32_t states[] = {
      BIT(BUTTON_DPAD_DOWN), 0, BIT(BUTTON_A), 0, BIT(BUTTON_DPAD_UP), 0, BIT(BUTTON_A), 0,
      BIT(BUTTON_DPAD_DOWN), 0, BIT(BUTTON_DPAD_DOWN), 0, BIT(BUTTON_A), 0, BIT(BUTTON_GUIDE),
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

/* queues one controller button event for SDL_PollEvent */
static void queue_button(uint32_t type, int button)
{
  memset(fake.queue, 0, sizeof fake.queue);
  fake.queue[0][0] = type;
  ((uint8_t *)&fake.queue[0][3])[0] = (uint8_t)button;
  fake.queued = 1;
  fake.queue_head = 0;
}

/* the button an event queued for pico-8 reaches it as, or -1 when it is dropped */
static int polled_button(uint32_t type, int button)
{
  uint32_t event[14];
  int got;

  queue_button(type, button);
  got = SDL_PollEvent(event) == 1 ? ((uint8_t *)&event[3])[0] : -1;
  fake.queued = fake.queue_head = 0;
  return got;
}

/*
 * The fixtures put PICO-8's Up on MENU+R1 and its X on the X button, which
 * pico-8 reads as the buttons it plays with, Up and B.
 */
static void test_controls(void *pad)
{
  int reads = fake.reads;

  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);
  fake.buttons[BUTTON_A] = 1;
  check(SDL_GameControllerGetButton(pad, BUTTON_A) == 1, "A still gives pico-8 its O");
  release_all();
  fake.buttons[BUTTON_X] = 1;
  check(SDL_GameControllerGetButton(pad, BUTTON_B) == 1 &&
            SDL_GameControllerGetButton(pad, BUTTON_X) == 0,
        "X gives pico-8 its X, read from B, and no longer reads as X");
  release_all();
  fake.buttons[BUTTON_B] = 1;
  check(SDL_GameControllerGetButton(pad, BUTTON_B) == 0 &&
            SDL_GameControllerGetButton(pad, BUTTON_A) == 0,
        "B, bound to no control, gives pico-8 nothing");
  release_all();
  fake.buttons[BUTTON_Y] = 1;
  check(SDL_GameControllerGetButton(pad, BUTTON_Y) == 0 &&
            SDL_GameControllerGetButton(pad, BUTTON_A) == 0,
        "nor does Y, which pico-8 would read as O");
  release_all();
  fake.buttons[BUTTON_DPAD_UP] = 1;
  check(SDL_GameControllerGetButton(pad, BUTTON_DPAD_UP) == 0, "Up on the d-pad, moved away, does nothing");
  release_all();
  fake.buttons[BUTTON_BACK] = 1;
  check(SDL_GameControllerGetButton(pad, BUTTON_BACK) == 1,
        "a button pico-8 does not play with is left alone");
  release_all();
  fake.axes[0] = -32768;
  check(SDL_GameControllerGetAxis(pad, 0) == -32768, "and so is the stick");
  release_all();

  fake.buttons[BUTTON_R1] = 1;
  check(SDL_GameControllerGetButton(pad, BUTTON_DPAD_UP) == 0, "R1 alone is not MENU+R1");
  fake.buttons[BUTTON_GUIDE] = 1;
  check(SDL_GameControllerGetButton(pad, BUTTON_DPAD_UP) == 1, "but with Menu held gives pico-8 Up");
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);
  SDL_RenderPresent(&renderer_b);
  check(fake.reads == reads, "and letting go of Menu then opens no menu");

  check(polled_button(SDL_CONTROLLERBUTTONDOWN, BUTTON_X) == BUTTON_B,
        "X's event reaches pico-8 as B's, as its state does");
  check(polled_button(SDL_CONTROLLERBUTTONUP, BUTTON_B) == -1 &&
            polled_button(SDL_CONTROLLERBUTTONDOWN, BUTTON_Y) == -1,
        "and the events of B and Y, bound to nothing, are dropped");
  check(polled_button(SDL_CONTROLLERBUTTONDOWN, BUTTON_A) == BUTTON_A &&
            polled_button(SDL_CONTROLLERBUTTONDOWN, BUTTON_BACK) == BUTTON_BACK,
        "while A's and Select's reach it as they were");
  check(polled_button(SDL_CONTROLLERBUTTONDOWN, BUTTON_L3) == -1,
        "and a shortcut's button still never reaches it");
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);
}

/*
 * Remapping in the menu: Options, up past Shortcuts to Controls, down to O,
 * A, then Y. B backs out of each page after.
 */
static void test_control_binding(void *pad)
{
  static const uint32_t bind[] = {
      BIT(BUTTON_DPAD_DOWN), 0, BIT(BUTTON_A), 0, BIT(BUTTON_DPAD_UP), 0, BIT(BUTTON_DPAD_UP), 0,
      BIT(BUTTON_A), 0, BIT(BUTTON_DPAD_DOWN), 0, BIT(BUTTON_DPAD_DOWN), 0, BIT(BUTTON_DPAD_DOWN),
      0, BIT(BUTTON_DPAD_DOWN), 0, BIT(BUTTON_A), 0, BIT(BUTTON_Y), 0, BIT(BUTTON_B), 0,
      BIT(BUTTON_B), 0, BIT(BUTTON_B)};
  /* the same way in, then up from Up to Restore Defaults */
  static const uint32_t restore[] = {
      BIT(BUTTON_DPAD_DOWN), 0, BIT(BUTTON_A), 0, BIT(BUTTON_DPAD_UP), 0, BIT(BUTTON_DPAD_UP), 0,
      BIT(BUTTON_A), 0, BIT(BUTTON_DPAD_UP), 0, BIT(BUTTON_A), 0, BIT(BUTTON_B), 0,
      BIT(BUTTON_B), 0, BIT(BUTTON_B)};
  char line[256] = "";
  FILE *file;
  int found = 0;

  release_all();
  tap_menu(pad);
  set_script(bind, (int)(sizeof bind / sizeof *bind));
  SDL_RenderPresent(&renderer_b);
  check(fake.delays == (int)(sizeof bind / sizeof *bind), "binding a control leaves the menu open");

  file = fopen(controls_path, "r");
  while (file != NULL && fgets(line, sizeof line, file) != NULL) {
    found |= strcmp(line, "o y\n") == 0;
  }
  if (file != NULL) {
    fclose(file);
  }
  check(found, "O is bound to Y in the controls file");

  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);
  fake.buttons[BUTTON_Y] = 1;
  check(SDL_GameControllerGetButton(pad, BUTTON_A) == 1, "and Y gives pico-8 its O straight away");
  release_all();
  fake.buttons[BUTTON_A] = 1;
  check(SDL_GameControllerGetButton(pad, BUTTON_A) == 0, "while A no longer does");
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);

  tap_menu(pad);
  set_script(restore, (int)(sizeof restore / sizeof *restore));
  SDL_RenderPresent(&renderer_b);
  check(fake.delays == (int)(sizeof restore / sizeof *restore) && access(controls_path, F_OK) != 0,
        "Restore Defaults removes the controls file");
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);
  fake.buttons[BUTTON_A] = 1;
  check(SDL_GameControllerGetButton(pad, BUTTON_A) == 1, "and A gives pico-8 its O again");
  release_all();
  fake.buttons[BUTTON_B] = 1;
  check(SDL_GameControllerGetButton(pad, BUTTON_B) == 1, "as B gives it its X");
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

static int state_exists(int slot)
{
  char path[8192];

  snprintf(path, sizeof path, "%s/states/Celeste.p8.st%d", dir, slot);
  return access(path, F_OK) == 0;
}

static int slot_named(const char *slot)
{
  char path[8192];

  snprintf(path, sizeof path, "%s/Celeste.p8.txt", switcher_dir);
  return file_is(path, slot);
}

/* a save is written in the background, and names its slot last of all */
/* how many threads this process has, the save's own among them while it writes */
static int thread_count(void)
{
  DIR *tasks = opendir("/proc/self/task");
  struct dirent *entry;
  int count = 0;

  while (tasks != NULL && (entry = readdir(tasks)) != NULL) {
    count += entry->d_name[0] != '.';
  }
  if (tasks != NULL) {
    closedir(tasks);
  }
  return count;
}

/*
 * The slot is named before the save's thread has finished, so this also waits
 * for the thread to end, by when the game knows the save is done
 */
static int written(const char *slot)
{
  int tries;

  for (tries = 0; tries < 500 && (!slot_named(slot) || thread_count() > 1); tries++) {
    usleep(10000);
  }
  return slot_named(slot);
}

/* a copy drawn into the bottom right corner, NextUI's 12 units in, as the save toast is */
static int toast_drawn(void)
{
  int i, margin = 12 * 3;

  for (i = 0; i < fake.copies && i < 256; i++) {
    if (fake.copy[i].has_dst && fake.copy[i].dst.x + fake.copy[i].dst.w == FAKE_OUTPUT_W - margin &&
        fake.copy[i].dst.y + fake.copy[i].dst.h == FAKE_OUTPUT_H - margin) {
      return 1;
    }
  }
  return 0;
}

static long state_size(int slot)
{
  char path[8192];
  struct stat info;

  snprintf(path, sizeof path, "%s/states/Celeste.p8.st%d", dir, slot);
  return stat(path, &info) == 0 ? (long)info.st_size : -1;
}

/*
 * The same in every run, as what pico-8 loads from pico8.dat is, and as hard
 * to pack, so a state that holds it is as big as it is
 */
static unsigned char big_byte(size_t i)
{
  uint32_t x = (uint32_t)i * 2654435761u + 7;

  x ^= x >> 13;
  x *= 0x5bd1e995u;
  return (unsigned char)(x ^ (x >> 15));
}

static void make_big(void)
{
  size_t i;

  big = malloc(BIG_SIZE);
  for (i = 0; big != NULL && i < BIG_SIZE; i++) {
    big[i] = big_byte(i);
  }
}

/* opens the menu, runs the script and lets every button go after */
static void run_script(void *pad, const uint32_t *states, int count)
{
  release_all();
  tap_menu(pad);
  set_script(states, count);
  SDL_RenderPresent(&renderer_a);
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);
}

/*
 * Down from Continue now stops on Save, Left and Right choose the slot, and
 * Load puts back what Save saved, in pico-8's memory and in its arena alike.
 * Quit then saves the slot the launchers resume, which resume reads, so this
 * runs first and last of its process.
 */
static void test_states(void *pad)
{
  static const uint32_t save_two[] = {BIT(BUTTON_DPAD_DOWN), 0, BIT(BUTTON_DPAD_RIGHT), 0,
                                      BIT(BUTTON_DPAD_RIGHT), 0, BIT(BUTTON_A)};
  static const uint32_t load[] = {BIT(BUTTON_DPAD_DOWN), 0, BIT(BUTTON_DPAD_DOWN), 0,
                                  BIT(BUTTON_A)};
  static const uint32_t load_empty[] = {BIT(BUTTON_DPAD_DOWN), 0, BIT(BUTTON_DPAD_DOWN), 0,
                                        BIT(BUTTON_DPAD_RIGHT), 0, BIT(BUTTON_A), 0,
                                        BIT(BUTTON_B)};
  static const uint32_t save_four[] = {BIT(BUTTON_DPAD_DOWN), 0, BIT(BUTTON_DPAD_RIGHT), 0,
                                       BIT(BUTTON_DPAD_RIGHT), 0, BIT(BUTTON_A)};
  static const uint32_t save_five[] = {BIT(BUTTON_DPAD_DOWN),  0, BIT(BUTTON_DPAD_RIGHT), 0,
                                       BIT(BUTTON_DPAD_RIGHT), 0, BIT(BUTTON_DPAD_RIGHT), 0,
                                       BIT(BUTTON_A)};
  static const uint32_t quit[] = {BIT(BUTTON_DPAD_UP), 0, BIT(BUTTON_A)};
  int pushed;

  /* Quit saves the resume slot here, which resume puts back */
  write_file(auto_save_path, "on\n", 3);
  make_big();
  L0_cart = &marker;
  fake.frame_colour = 0x00205080;
  SDL_RenderPresent(&renderer_a);
  check(slot_named("0"), "with no state yet the switcher gets the artwork as slot 0");

  marker = 1;
  heap = malloc(32);
  snprintf(heap, 32, "saved");
  fake.audio_locks = fake.audio_unlocks = 0;
  run_script(pad, save_two, 7);
  check(written("2") && state_exists(2), "Save on the slot two to the right saves slot 2");
  check(state_size(2) > 0 && state_size(2) < (long)BIG_SIZE,
        "leaving out what pico-8 loaded before its cart, which is as it was");

  fake.copies = 0;
  SDL_RenderPresent(&renderer_a);
  check(toast_drawn() && fake.last_alpha == 255,
        "once it is written the game shows it saved, in the bottom right corner");
  fake.ticks += 1200;
  fake.copies = 0;
  SDL_RenderPresent(&renderer_a);
  check(toast_drawn() && fake.last_alpha > 0 && fake.last_alpha < 255,
        "and fades that out after a second");
  fake.ticks += 1000;
  fake.copies = 0;
  SDL_RenderPresent(&renderer_a);
  check(!toast_drawn(), "until it is gone");
  check(preview_is_frame(2, 341, 256, 0x00205080) && slot_named("2"),
        "with the game as its preview, named as the slot to resume");
  check(fake.audio_locks == 1 && fake.audio_unlocks == 1, "pico-8's audio is held while it saves");

  marker = 2;
  snprintf(heap, 32, "changed");
  run_script(pad, load, 5);
  check(marker == 1, "Load on the slot last saved puts pico-8's memory back");
  check(heap != NULL && strcmp(heap, "saved") == 0, "and what it allocated");
  check(slot_named("2"), "and names that slot to resume");

  marker = 3;
  run_script(pad, load_empty, 9);
  check(marker == 3 && fake.delays == 9, "Load on an empty slot loads nothing and stays open");

  num_threads = 1;
  run_script(pad, save_four, 7);
  check(!state_exists(4) && slot_named("2"), "nothing is saved while pico-8 is downloading");
  num_threads = 0;

  big[0] ^= 0xff;
  run_script(pad, save_five, 9);
  check(written("5") && state_size(5) > (long)BIG_SIZE,
        "once that has changed, a state holds it too");
  big[0] ^= 0xff;

  marker = 7;
  pushed = fake.pushed;
  run_script(pad, quit, 3);
  check(fake.pushed == pushed + 1 && fake.pushed_type == SDL_QUIT, "Quit still quits pico-8");
  check(state_exists(9) && preview_is_frame(9, 341, 256, 0x00205080) && slot_named("9"),
        "after saving slot 9 with Auto Save on Exit on, with its preview, for the launchers to resume");
  check(access(auto_resume_path, F_OK) != 0, "Quit does not start the cart again at boot");
}

/* the slot launch.sh names is put back once the cart has started, in a new process */
static void test_resume(void *pad)
{
  int black_clears = fake.black_clears;

  setenv("PICO_PAK_RESUME_SLOT", "9", 1);
  /* a different config moves what pico-8 loads from pico8.dat, as this moves the block */
  check(malloc(4096) != NULL, "this run allocates before the block the last run left out");
  make_big();
  marker = 100;
  SDL_RenderPresent(&renderer_a);
  check(marker == 100 && fake.black_clears == black_clears + 1,
        "the screen is black until the cart starts, with nothing put back");
  L0_cart = &marker;
  SDL_RenderPresent(&renderer_a);
  check(marker == 100, "nor on the frame it starts");
  SDL_RenderPresent(&renderer_a);
  check(marker == 7 && heap != NULL && strcmp(heap, "saved") == 0,
        "then the slot is put back, as it was when the last run quit");
  check(big != NULL && big[0] == big_byte(0) && big[BIG_SIZE - 1] == big_byte(BIG_SIZE - 1),
        "with what it left out as this run loaded it, wherever this run put it");
  (void)pad;
}

/* power control's SIGTERM saves slot 9 and quits, and the cart starts again at boot */
static void test_shutdown(void *pad)
{
  char rom[256] = "";
  FILE *file;
  int pushed = fake.pushed, status;
  pid_t child;

  /* a shutdown saves whatever Auto Save on Exit is set to, as minarch does on powering off */
  write_file(auto_save_path, "off\n", 4);
  SDL_RenderPresent(&renderer_a);

  /* a pico-8 that never presents again is still ended, a little after the signal */
  fflush(stdout);
  child = fork();
  if (child == 0) {
    raise(SIGTERM);
    sleep(10);
    _exit(0);
  }
  check(child > 0 && waitpid(child, &status, 0) == child && WIFSIGNALED(status) &&
            WTERMSIG(status) == SIGALRM,
        "SIGTERM ends a pico-8 that presents no frame to save from");

  marker = 11;
  raise(SIGTERM);
  check(fake.pushed == pushed, "SIGTERM no longer ends pico-8 by itself");
  SDL_RenderPresent(&renderer_a);
  check(fake.pushed == pushed + 1 && fake.pushed_type == SDL_QUIT,
        "the next frame quits pico-8");
  check(state_exists(9) && slot_named("9"), "after saving slot 9");
  file = fopen(auto_resume_path, "r");
  if (file != NULL) {
    if (fgets(rom, sizeof rom, file) == NULL) {
      rom[0] = '\0';
    }
    fclose(file);
  }
  check(strcmp(rom, "/Roms/Pico-8 (PICO)/Celeste.p8") == 0,
        "and naming the cart to start at boot, with no line ending");
  (void)pad;
}

/* with Auto Save on Exit off, as it starts, Quit saves nothing and leaves the slot to resume alone */
static void test_quit_without_saving(void *pad)
{
  static const uint32_t quit[] = {BIT(BUTTON_DPAD_UP), 0, BIT(BUTTON_A)};
  char path[8192];
  int pushed = fake.pushed;

  unlink(auto_save_path);
  snprintf(path, sizeof path, "%s/states/Celeste.p8.st9", dir);
  unlink(path);
  snprintf(path, sizeof path, "%s/Celeste.p8.0.bmp", switcher_dir);
  unlink(path);
  SDL_RenderPresent(&renderer_a);
  run_script(pad, quit, 3);
  check(fake.pushed == pushed + 1 && fake.pushed_type == SDL_QUIT, "Quit quits pico-8");
  check(!state_exists(9) && slot_named("9"), "without saving, leaving the slot to resume as it was");
  check(access(path, F_OK) != 0, "and gives the switcher no frame, as the cart has states");
}

/* how many calls to GL or SDL_RenderFlush named starting with name, from the from'th */
static int count_calls(const char *name, int from)
{
  int count = 0, i;

  for (i = from; i < fake.gl_call_count; i++) {
    count += strncmp(fake.gl_calls[i].name, name, strlen(name)) == 0;
  }
  return count;
}

/* the first such call from the from'th, or -1 */
static int find_call(const char *name, int from)
{
  int i;

  for (i = from; i < fake.gl_call_count; i++) {
    if (strcmp(fake.gl_calls[i].name, name) == 0) {
      return i;
    }
  }
  return -1;
}

static char shaders_path[4096];

/*
 * NextUI's layout: crt runs a.glsl at 2x then b.glsl at the screen's size,
 * and broken runs a shader that will not build. Cycle Shader is MENU+L1.
 */
static void make_shader_fixtures(void)
{
  static const char shortcuts[] = "screen-mode l1\nshader menu+l1\n";
  char path[8192];

  snprintf(path, sizeof path, "%s/Shaders", dir);
  make_dir(path);
  setenv("PICO_PAK_SHADERS_DIR", path, 1);
  snprintf(path, sizeof path, "%s/Shaders/glsl", dir);
  make_dir(path);
  snprintf(path, sizeof path, "%s/Shaders/glsl/a.glsl", dir);
  write_file(path, "#version 130\n#pragma parameter SCANLINE \"Scanline\" 0.25 0.0 1.0 0.05\nA\n",
             strlen("#version 130\n#pragma parameter SCANLINE \"Scanline\" 0.25 0.0 1.0 0.05\nA\n"));
  snprintf(path, sizeof path, "%s/Shaders/glsl/b.glsl", dir);
  write_file(path, "B\n", 2);
  snprintf(path, sizeof path, "%s/Shaders/glsl/bad.glsl", dir);
  write_file(path, "FAIL\n", 5);
  snprintf(path, sizeof path, "%s/Shaders/crt.cfg", dir);
  write_file(path,
             "minarch_nrofshaders = 2\nminarch_shader1 = a.glsl\nminarch_shader1_upscale = 2\n"
             "minarch_shader2 = b.glsl\nminarch_shader2_upscale = screen",
             strlen("minarch_nrofshaders = 2\nminarch_shader1 = a.glsl\nminarch_shader1_upscale = 2\n"
                    "minarch_shader2 = b.glsl\nminarch_shader2_upscale = screen"));
  snprintf(path, sizeof path, "%s/Shaders/zbroken.cfg", dir);
  write_file(path, "minarch_nrofshaders = 1\nminarch_shader1 = bad.glsl\n",
             strlen("minarch_nrofshaders = 1\nminarch_shader1 = bad.glsl\n"));

  snprintf(shaders_path, sizeof shaders_path, "%s/shaders", dir);
  setenv("PICO_PAK_SHADER_SETTINGS", shaders_path, 1);
  write_file(shortcuts_path, shortcuts, strlen(shortcuts));
}

/* a frame as pico-8 draws one: its screen uploaded, cleared to, copied into its square and shown */
static void play_frame(void *renderer, void *texture, uint32_t colour)
{
  static uint32_t pixels[128 * 128];
  fake_rect square = {128, 0, 768, 768};
  size_t i;

  for (i = 0; i < sizeof pixels / sizeof *pixels; i++) {
    pixels[i] = colour;
  }
  SDL_UpdateTexture(texture, NULL, pixels, 128 * 4);
  SDL_RenderCopy(renderer, texture, NULL, &square);
  SDL_RenderPresent(renderer);
}

/* presses MENU+L1, Cycle Shader, and lets it all go */
static void cycle_shader(void *pad)
{
  release_all();
  fake.buttons[BUTTON_GUIDE] = 1;
  SDL_GameControllerGetButton(pad, BUTTON_A);
  fake.buttons[BUTTON_L1] = 1;
  SDL_GameControllerGetButton(pad, BUTTON_L1);
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);
}

/* whether the copies from the from'th draw a notice at the top right */
static int notice_drawn(int from)
{
  int i, drawn = 0;

  for (i = from; i < fake.copies; i++) {
    drawn |= fake.copy[i].has_dst && fake.copy[i].dst.h == 90 &&
             fake.copy[i].dst.x + fake.copy[i].dst.w == 1024 - 15 && fake.copy[i].dst.y == 15;
  }
  return drawn;
}

/* whether a copy from the from'th was of texture */
static int copied(void *texture, int from)
{
  int i;

  for (i = from; i < fake.copies; i++) {
    if (fake.copy[i].texture == texture) {
      return 1;
    }
  }
  return 0;
}

static void test_shaders(void *pad)
{
  struct fake_gl_state before = {.program = 7, .active_texture = 0x84C0 + 1, .textures = {3, 4},
                                 .array_buffer = 5, .framebuffer = 6, .viewport = {1, 2, 3, 4},
                                 .blend = 1, .scissor = 1, .attribs = {1, 1, 0, 1}};
  void *game = SDL_CreateTexture(&renderer_a, SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_STREAMING, 128, 128);
  void *fps = SDL_CreateTexture(&renderer_a, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, 128, 8);
  fake_rect strip = {0, 0, 128, 8};
  int from, at, draw, flush, i, uniform;

  /* off, as the settings start */
  fake.copies = 0;
  play_frame(&renderer_a, game, 0xff112233);
  check(copied(game, 0) && count_calls("glDrawArrays", 0) == 0,
        "with the shaders off pico-8's own copy is drawn and GL draws nothing");
  check(strncmp(fake.gl_sources[0], "#version 100", 12) == 0,
        "but the copy to the screen is built, as there are shaders to draw");

  cycle_shader(pad);
  SDL_RenderPresent(&renderer_a);
  check(file_has(shaders_path, "minarch_shaders_preset = crt.cfg") &&
            file_has(shaders_path, "minarch_nrofshaders = 2"),
        "MENU+L1 cycles to the first preset and saves it");

  fake.gl = before;
  fake.copies = 0;
  fake.ticks += 2000;
  from = fake.gl_call_count;
  {
    static uint32_t pixels[128 * 128];
    fake_rect square = {128, 0, 768, 768};

    for (i = 0; i < 128 * 128; i++) {
      pixels[i] = 0xff112233;
    }
    SDL_UpdateTexture(game, NULL, pixels, 128 * 4);
    SDL_UpdateTexture(fps, NULL, pixels, 128 * 4);
    SDL_RenderCopy(&renderer_a, game, NULL, &square);
    SDL_RenderCopy(&renderer_a, fps, NULL, &strip);
    SDL_RenderPresent(&renderer_a);
  }
  draw = find_call("glDrawArrays", from);
  flush = find_call("SDL_RenderFlush", from);
  check(draw >= 0 && flush >= 0 && flush < find_call("glViewport", from),
        "the next frame is drawn through the shaders, after what SDL queued before it");
  at = find_call("glTexImage2D", from);
  check(at >= 0 && fake.gl_calls[at].args[1] == 128 && fake.gl_calls[at].args[2] == 128 &&
            fake.gl_calls[at].args[3] &&
            memcmp(fake.gl_calls[at].pixel, "\x11\x22\x33\xff", 4) == 0,
        "from pico-8's 128x128 frame, in GL's byte order");
  check(count_calls("glDrawArrays", from) == 3, "one draw for each pass and one to the screen");
  at = find_call("glViewport", from);
  check(at >= 0 && fake.gl_calls[at].args[2] == 256 && fake.gl_calls[at].args[3] == 256,
        "a.glsl at 2x draws 256x256");
  at = find_call("glViewport", at + 1);
  check(at >= 0 && fake.gl_calls[at].args[2] == 768 && fake.gl_calls[at].args[3] == 768 &&
            fake.gl_calls[at].args[0] == 0,
        "b.glsl at the screen's size draws as big as pico-8's square");
  at = find_call("glViewport", at + 1);
  check(at >= 0 && fake.gl_calls[at].args[0] == 128 && fake.gl_calls[at].args[1] == 0 &&
            fake.gl_calls[at].args[2] == 768 && fake.gl_calls[at].args[3] == 768 &&
            find_call("glBindFramebuffer", from) >= 0,
        "and the last draw fills pico-8's square on the screen");
  at = find_call("glDrawArrays", draw + 1);
  at = find_call("glDrawArrays", at + 1);
  check(at >= 0 && fake.gl_calls[at].args[3] == 6, "into the framebuffer SDL draws to");
  uniform = -1;
  for (i = 0; i < fake.uniform_count; i++) {
    if (strcmp(fake.uniforms[i], "SCANLINE") == 0) {
      uniform = i;
    }
  }
  at = find_call("glUniform1f", from);
  while (at >= 0 && fake.gl_calls[at].args[0] != uniform) {
    at = find_call("glUniform1f", at + 1);
  }
  check(uniform >= 0 && at >= 0 && fake.gl_calls[at].floats[0] > 0.24f &&
            fake.gl_calls[at].floats[0] < 0.26f,
        "a shader's parameter is set to its default");
  check(!copied(game, 0) && copied(fps, 0), "pico-8's copy of its screen is skipped, and not its frame rate");
  check(fake.gl.program == before.program && fake.gl.active_texture == before.active_texture &&
            fake.gl.textures[0] == before.textures[0] && fake.gl.textures[1] == before.textures[1] &&
            fake.gl.array_buffer == before.array_buffer &&
            fake.gl.framebuffer == before.framebuffer &&
            memcmp(fake.gl.viewport, before.viewport, sizeof before.viewport) == 0 &&
            fake.gl.blend && fake.gl.scissor &&
            memcmp(fake.gl.attribs, before.attribs, sizeof before.attribs) == 0,
        "and every piece of GL state SDL keeps track of is put back");
  check(file_is(choice_path, "b.png") && fake.copies >= 2, "the overlay is still drawn over it");
  for (i = 0; i < fake.gl_source_count; i++) {
    if (strncmp(fake.gl_sources[i], "#version 300 es\n#define VERTEX", 30) == 0) {
      break;
    }
  }
  check(i < fake.gl_source_count, "a.glsl's desktop version is built as GLSL ES 3");

  /* the second frame only replaces the pixels of the first */
  from = fake.gl_call_count;
  play_frame(&renderer_a, game, 0xff445566);
  at = find_call("glTexSubImage2D", from);
  check(at >= 0 && count_calls("glTexImage2D", from) == 0 &&
            memcmp(fake.gl_calls[at].pixel, "\x44\x55\x66\xff", 4) == 0,
        "the next frame is uploaded over the last without making its texture again");
  check(count_calls("glCompileShader", from) == 0, "nor building the shaders again");

  /* L1 stretches the game, and the shaders draw over the whole screen */
  release_all();
  fake.buttons[BUTTON_L1] = 1;
  SDL_GameControllerGetButton(pad, BUTTON_L1);
  SDL_RenderPresent(&renderer_a);
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);
  from = fake.gl_call_count;
  play_frame(&renderer_a, game, 0xff445566);
  at = find_call("glViewport", find_call("glViewport", find_call("glViewport", from) + 1) + 1);
  check(file_is(screen_mode_path, "stretched") && at >= 0 && fake.gl_calls[at].args[0] == 0 &&
            fake.gl_calls[at].args[1] == 0 && fake.gl_calls[at].args[2] == FAKE_OUTPUT_W &&
            fake.gl_calls[at].args[3] == FAKE_OUTPUT_H,
        "stretched, the shaders draw over the whole screen");
  fake.buttons[BUTTON_L1] = 1;
  SDL_GameControllerGetButton(pad, BUTTON_L1);
  SDL_RenderPresent(&renderer_a);
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_A);

  /* the menu reads back what the shaders drew */
  release_all();
  from = fake.gl_call_count;
  change_option(pad, &renderer_a, GROUP_SHADERS, 0, 1);
  check(file_has(shaders_path, "minarch_shaders_preset = zbroken.cfg"),
        "Right on the Shaders group's preset picks the next one");

  fake.ticks += 2000;
  fake.copies = 0;
  from = fake.gl_call_count;
  play_frame(&renderer_a, game, 0xff445566);
  check(count_calls("glDrawArrays", from) == 2 && !copied(game, 0) && notice_drawn(0),
        "a shader that will not build is passed through, with a notice that it cannot run");
  fake.ticks += 2000;
  fake.copies = 0;
  play_frame(&renderer_a, game, 0xff445566);
  check(!notice_drawn(0), "which is only given once");

  cycle_shader(pad);
  SDL_RenderPresent(&renderer_a);
  check(file_has(shaders_path, "minarch_nrofshaders = off"), "MENU+L1 past the last preset is None");
  fake.copies = 0;
  from = fake.gl_call_count;
  play_frame(&renderer_a, game, 0xff445566);
  check(copied(game, 0) && count_calls("glDrawArrays", from) == 0,
        "which leaves pico-8's own copy in place again");

  /* a renderer that is not SDL's GLES one cannot draw them */
  SDL_DestroyRenderer(&renderer_a);
  fake.renderer_name = "software";
  game = SDL_CreateTexture(&renderer_b, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 128,
                           128);
  cycle_shader(pad);
  fake.ticks += 2000;
  fake.copies = 0;
  from = fake.gl_call_count;
  SDL_RenderPresent(&renderer_b);
  check(file_has(shaders_path, "minarch_nrofshaders = off") && notice_drawn(0),
        "on the software renderer MENU+L1 changes nothing and says why");
  fake.copies = 0;
  play_frame(&renderer_b, game, 0xff445566);
  check(copied(game, 0) && count_calls("gl", from) == 0, "and GL is never touched");
}

int main(int argc, char **argv)
{
  const char *mode = argc > 1 ? argv[1] : "";
  int states = strcmp(mode, "states") == 0 || strcmp(mode, "resume") == 0 ||
               strcmp(mode, "shutdown") == 0 || strcmp(mode, "quit") == 0;
  void *pad;

  tally = mmap(NULL, sizeof *tally, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (tally == MAP_FAILED) {
    perror("mmap");
    return 2;
  }
  make_fixtures(states);
  if (strcmp(mode, "shaders") == 0) {
    make_shader_fixtures();
  }

  pad = SDL_GameControllerOpen(0);
  check(pad != NULL, "the pad pico-8 opens is passed through");
  check(SDL_OpenAudioDevice(NULL, 0, NULL, NULL, 0) == FAKE_AUDIO_DEVICE,
        "the audio device pico-8 opens is passed through");

  if (strcmp(mode, "quit-event") == 0) {
    test_quit_event(pad);
  } else if (strcmp(mode, "quit-shortcut") == 0) {
    test_quit_shortcut(pad);
  } else if (strcmp(mode, "states") == 0) {
    test_states(pad);
  } else if (strcmp(mode, "resume") == 0) {
    test_resume(pad);
  } else if (strcmp(mode, "shutdown") == 0) {
    test_shutdown(pad);
  } else if (strcmp(mode, "quit") == 0) {
    test_quit_without_saving(pad);
  } else if (strcmp(mode, "shaders") == 0) {
    test_shaders(pad);
  }
  if (*mode != '\0') {
    printf("1..%d\n", tally->tests);
    return tally->failures == 0 ? 0 : 1;
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
  test_controls(pad);
  test_control_binding(pad);
  test_indicator();
  test_quit(pad);
  test_crash();
  test_closed_pad(pad);
  check(count_calls("gl", 0) == 0, "with no shaders GL is never touched");

  printf("1..%d\n", tally->tests);
  return tally->failures == 0 ? 0 : 1;
}
