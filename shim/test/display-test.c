/*
 * Plays pico-8's part against sdl-display.so, which is preloaded over the fake
 * libSDL2 in fake-sdl.c, and checks what reached the fake. Run once per value of
 * PICO_PAK_MENU_BUTTON the shim accepts:
 *
 *   LD_PRELOAD=sdl-display.so display-test guide|start|none
 *
 * guide also runs the overlay, screen mode and event checks, which do not
 * depend on the Menu button. The shim reads its environment once, so each
 * mode needs a process of its own. Prints TAP and exits non-zero on a failure.
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fake-sdl.h"

/* From SDL_events.h and SDL_gamecontroller.h. */
#define SDL_QUIT 0x100
#define SDL_KEYDOWN 0x300
#define SDL_CONTROLLERBUTTONDOWN 0x650
#define BUTTON_BACK 4
#define BUTTON_GUIDE 5
#define BUTTON_START 6
#define BUTTON_DPAD_UP 11
#define BUTTON_DPAD_DOWN 12
#define BUTTON_DPAD_LEFT 13
#define BUTTON_DPAD_RIGHT 14

/* what pico-8 calls, answered by the shim and passed on to the fake */
void SDL_RenderPresent(void *renderer);
void SDL_DestroyRenderer(void *renderer);
int SDL_PollEvent(void *event);
int SDL_RenderCopy(void *renderer, void *texture, const fake_rect *src, const fake_rect *dst);
void *SDL_GameControllerOpen(int index);
void SDL_GameControllerClose(void *controller);
uint8_t SDL_GameControllerGetButton(void *controller, int button);

static int tests;
static int failures;
static char dir[] = "/tmp/display-test.XXXXXX";
static char choice_path[4096];
static char screen_mode_path[4096];
static char renderer_a, renderer_b, game_texture;

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
  char path[4096];
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
  char path[4096];
  size_t i;

  if (mkdtemp(dir) == NULL) {
    perror("mkdtemp");
    exit(2);
  }
  snprintf(path, sizeof path, "%s/overlays", dir);
  if (mkdir(path, 0755) != 0) {
    perror(path);
    exit(2);
  }

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

  snprintf(path, sizeof path, "%s/overlays", dir);
  setenv("PICO_PAK_OVERLAY_DIR", path, 1);
  setenv("PICO_PAK_OVERLAY_CHOICE", choice_path, 1);
  setenv("PICO_PAK_SCREEN_MODE", screen_mode_path, 1);
}

static void release_all(void)
{
  memset(fake.buttons, 0, sizeof fake.buttons);
}

/* presses a button with Select held, as pico-8 would see it read that frame */
static uint8_t combo(void *pad, int button)
{
  fake.buttons[BUTTON_BACK] = 1;
  fake.buttons[button] = 1;
  return SDL_GameControllerGetButton(pad, button);
}

static void release(void *pad, int button)
{
  fake.buttons[button] = 0;
  SDL_GameControllerGetButton(pad, button);
}

static void test_menu(void *pad, const char *mode)
{
  if (strcmp(mode, "start") == 0) {
    check(combo(pad, BUTTON_START) == 0, "start: Select + Start is hidden from pico-8");
    check(fake.pushed == 1 && fake.pushed_type == SDL_QUIT, "start: Select + Start pushes SDL_QUIT");
    check(SDL_GameControllerGetButton(pad, BUTTON_START) == 0 && fake.pushed == 1,
          "start: a held Start quits only once");
    release_all();
    fake.buttons[BUTTON_GUIDE] = 1;
    check(combo(pad, BUTTON_GUIDE) == 1 && fake.pushed == 1,
          "start: Select + Guide reaches pico-8 and does not quit");
  } else if (strcmp(mode, "guide") == 0) {
    check(combo(pad, BUTTON_GUIDE) == 0, "guide: Select + Guide is hidden from pico-8");
    check(fake.pushed == 1 && fake.pushed_type == SDL_QUIT, "guide: Select + Guide pushes SDL_QUIT");
    check(SDL_GameControllerGetButton(pad, BUTTON_GUIDE) == 0 && fake.pushed == 1,
          "guide: a held Guide quits only once");
    release_all();
    check(combo(pad, BUTTON_START) == 1 && fake.pushed == 1,
          "guide: Select + Start reaches pico-8 and does not quit");
  } else {
    check(combo(pad, BUTTON_START) == 1 && fake.pushed == 0,
          "none: Select + Start reaches pico-8 and does not quit");
    release_all();
    check(combo(pad, BUTTON_GUIDE) == 1 && fake.pushed == 0,
          "none: Select + Guide reaches pico-8 and does not quit");
  }
  release_all();
}

static void test_overlays(void *pad)
{
  struct fake_copy *copy;
  int copies;

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

  check(combo(pad, BUTTON_DPAD_RIGHT) == 0, "Select + Right is hidden from pico-8");
  check(file_is(choice_path, "z.png"), "Select + Right moves to z.png and saves it");
  copies = fake.copies;
  SDL_RenderPresent(&renderer_a);
  check(fake.copies == copies && fake.textures_live == 0,
        "an unreadable PNG draws nothing and frees the old texture");
  SDL_RenderPresent(&renderer_a);
  check(fake.copies == copies && fake.surfaces == 1, "an unreadable PNG is not retried every frame");

  fake.buttons[BUTTON_DPAD_RIGHT] = 0;
  check(SDL_GameControllerGetButton(pad, BUTTON_DPAD_RIGHT) == 0,
        "Right stays hidden while Select is held");
  check(combo(pad, BUTTON_DPAD_RIGHT) == 0 && file_is(choice_path, "disabled"),
        "the next Select + Right disables the overlay");
  SDL_RenderPresent(&renderer_a);
  check(fake.copies == copies, "nothing is drawn while disabled");

  release(pad, BUTTON_DPAD_RIGHT);
  check(combo(pad, BUTTON_DPAD_RIGHT) == 0 && file_is(choice_path, "a.png"),
        "Select + Right wraps around to a.png, skipping ._ files and other types");
  SDL_RenderPresent(&renderer_a);
  copy = &fake.copy[fake.copies - 1];
  check(fake.surfaces == 2 && fake.surface_w == 4 && fake.surface_h == 3 &&
            memcmp(fake.surface_pixels, wide_pixels, sizeof wide_pixels) == 0,
        "a.png is decoded to its pixels");
  check(fake.copies == copies + 1 && rect_is(copy->dst, 0, 0, FAKE_OUTPUT_W, FAKE_OUTPUT_H),
        "a 4:3 overlay fills a 4:3 screen");

  release_all();
  check(combo(pad, BUTTON_DPAD_LEFT) == 0 && file_is(choice_path, "disabled"),
        "Select + Left steps back from the first overlay to disabled");
  release_all();
  combo(pad, BUTTON_DPAD_RIGHT);
  check(file_is(choice_path, "a.png"), "Select + Right steps forward again");

  /* a press that began with Select stays hidden until a frame sees it released */
  release_all();
  SDL_GameControllerGetButton(pad, BUTTON_DPAD_RIGHT);
  fake.buttons[BUTTON_DPAD_RIGHT] = 1;
  check(SDL_GameControllerGetButton(pad, BUTTON_DPAD_RIGHT) == 1 && file_is(choice_path, "a.png"),
        "Right alone reaches pico-8 and changes nothing");
  release_all();

  SDL_DestroyRenderer(&renderer_a);
  copies = fake.copies;
  SDL_RenderPresent(&renderer_b);
  check(fake.surfaces == 3 && fake.copies == copies + 1,
        "a new renderer gets the overlay loaded again");
}

static void test_events(void *pad)
{
  uint32_t event[14];

  (void)pad;
  memset(fake.queue, 0, sizeof fake.queue);
  fake.queue[0][0] = SDL_CONTROLLERBUTTONDOWN;
  ((uint8_t *)&fake.queue[0][3])[0] = BUTTON_DPAD_LEFT;
  fake.queue[1][0] = SDL_KEYDOWN;
  fake.queue[2][0] = SDL_CONTROLLERBUTTONDOWN;
  ((uint8_t *)&fake.queue[2][3])[0] = BUTTON_DPAD_LEFT;
  fake.queued = 2;
  fake.queue_head = 0;

  fake.buttons[BUTTON_BACK] = 1;
  check(SDL_PollEvent(event) == 1 && event[0] == SDL_KEYDOWN,
        "a d-pad event is swallowed while Select is held");
  check(SDL_PollEvent(event) == 0, "nothing else is left in the queue");

  release_all();
  fake.queued = 3;
  fake.queue_head = 2;
  check(SDL_PollEvent(event) == 1 && event[0] == SDL_CONTROLLERBUTTONDOWN,
        "a d-pad event without Select reaches pico-8");
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

  check(combo(pad, BUTTON_DPAD_DOWN) == 0, "Select + Down is hidden from pico-8");
  check(file_is(screen_mode_path, "stretched"), "Select + Down switches to stretched and saves it");
  release_all();

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

  check(combo(pad, BUTTON_DPAD_UP) == 0 && file_is(screen_mode_path, "standard"),
        "Select + Up switches back to standard");
  release_all();
  SDL_RenderCopy(&renderer_b, &game_texture, NULL, &game);
  copy = &fake.copy[fake.copies - 1];
  check(rect_is(copy->dst, 128, 0, 768, 768), "standard mode stops stretching");
}

static void test_closed_pad(void *pad)
{
  SDL_GameControllerClose(pad);
  check(combo(pad, BUTTON_DPAD_RIGHT) == 1 && file_is(choice_path, "a.png"),
        "a closed pad is no longer read for combos");
  release_all();
}

int main(int argc, char **argv)
{
  const char *mode = argc > 1 ? argv[1] : "none";
  void *pad;

  make_fixtures();
  if (strcmp(mode, "none") == 0) {
    unsetenv("PICO_PAK_MENU_BUTTON");
  } else {
    setenv("PICO_PAK_MENU_BUTTON", mode, 1);
  }

  pad = SDL_GameControllerOpen(0);
  check(pad != NULL, "the pad pico-8 opens is passed through");

  test_menu(pad, mode);
  if (strcmp(mode, "guide") == 0) {
    test_overlays(pad);
    test_events(pad);
    test_screen_mode(pad);
    test_closed_pad(pad);
  }

  printf("1..%d\n", tests);
  return failures == 0 ? 0 : 1;
}
