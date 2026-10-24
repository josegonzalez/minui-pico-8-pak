/*
 * Draws a PNG over every frame pico-8 presents, for bezels and overlays.
 *
 * pico-8 has no overlay support of its own, but it draws through SDL's
 * renderer, so wrapping SDL_RenderPresent lets this copy an image over the
 * finished frame just before it reaches the screen. The image is scaled to fit
 * the screen without changing its shape and needs a transparent window where
 * the game shows through. The PNG is decoded here with stb_image, so overlays
 * need nothing on the device beyond the SDL2 pico-8 already uses.
 *
 * launch.sh passes the overlay folder in PICO_PAK_OVERLAY_DIR. Select +
 * Left/Right steps through every PNG there and a disabled state, and the choice
 * is saved by name to the file in PICO_PAK_OVERLAY_CHOICE for the next launch.
 * pico-8 reads the pad with SDL_GameControllerGetButton rather than from
 * events, so the combo is read the same way and hidden from those calls.
 *
 * Select + Up/Down switches between the standard and stretched screen modes,
 * saved to the file in PICO_PAK_SCREEN_MODE. pico-8 only takes its draw rect at
 * startup, so launch.sh always starts it in standard mode and stretching is
 * done here by redirecting the copy pico-8 draws each frame with.
 *
 * Select + Menu quits pico-8 from a game, Splore or its command prompt by
 * pushing SDL_QUIT, the event closing its window sends. Which pad button is
 * Menu depends on the platform's controller mapping, so launch.sh names it in
 * PICO_PAK_MENU_BUTTON, and without one there is no quit combo.
 *
 * Every SDL call goes through dlsym, like sdl-nosensor.c, so this builds without
 * SDL headers and asks glibc for nothing newer than that shim does. See
 * shim/Dockerfile.
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Only the PNG decoder is compiled, static so none of it is exported into
 * pico-8, and without the HDR paths that would pull in libm. Its warnings are
 * its own, so they are silenced for the include alone.
 */
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
/* STBI_ONLY_PNG implies it, but too late to keep its declaration out */
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stb_image.h"
#pragma GCC diagnostic pop

/* From SDL_rect.h, SDL_blendmode.h, SDL_events.h and SDL_gamecontroller.h. */
typedef struct {
  int x, y, w, h;
} SDL_Rect;
typedef struct {
  uint32_t type;
  uint32_t timestamp;
  int32_t which;
  uint8_t button;
  uint8_t state;
} SDL_ControllerButtonEvent;
#define SDL_BLENDMODE_BLEND 0x00000001
#define SDL_SCALEMODE_LINEAR 1
#define SDL_QUIT 0x100
#define SDL_CONTROLLERBUTTONDOWN 0x650
#define SDL_CONTROLLERBUTTONUP 0x651
#define SDL_CONTROLLER_BUTTON_BACK 4
#define SDL_CONTROLLER_BUTTON_GUIDE 5
#define SDL_CONTROLLER_BUTTON_START 6
#define SDL_CONTROLLER_BUTTON_DPAD_UP 11
#define SDL_CONTROLLER_BUTTON_DPAD_DOWN 12
#define SDL_CONTROLLER_BUTTON_DPAD_LEFT 13
#define SDL_CONTROLLER_BUTTON_DPAD_RIGHT 14

/* indexes into the state of the buttons pressed with Select below */
enum { DIR_LEFT, DIR_RIGHT, DIR_UP, DIR_DOWN, DIR_MENU, DIR_COUNT };

static const char *const screen_modes[] = {"standard", "stretched"};
#define SCREEN_MODE_COUNT ((int)(sizeof screen_modes / sizeof *screen_modes))
#define SCREEN_MODE_STRETCHED 1

static struct {
  void (*RenderPresent)(void *);
  void (*DestroyRenderer)(void *);
  int (*PollEvent)(void *);
  int (*PushEvent)(void *);
  void *(*GameControllerOpen)(int);
  void (*GameControllerClose)(void *);
  uint8_t (*GameControllerGetButton)(void *, int);
  void *(*CreateRGBSurfaceFrom)(void *, int, int, int, int, uint32_t, uint32_t, uint32_t,
                                uint32_t);
  void *(*CreateTextureFromSurface)(void *, void *);
  void (*DestroyTexture)(void *);
  void (*FreeSurface)(void *);
  int (*SetTextureBlendMode)(void *, int);
  int (*SetTextureScaleMode)(void *, int);
  const char *(*GetError)(void);
  int (*RenderCopy)(void *, void *, const SDL_Rect *, const SDL_Rect *);
  void (*RenderGetLogicalSize)(void *, int *, int *);
  int (*RenderSetLogicalSize)(void *, int, int);
  void (*RenderGetViewport)(void *, SDL_Rect *);
  int (*RenderSetViewport)(void *, const SDL_Rect *);
  void (*RenderGetScale)(void *, float *, float *);
  int (*RenderSetScale)(void *, float, float);
  int (*RenderIsClipEnabled)(void *);
  void (*RenderGetClipRect)(void *, SDL_Rect *);
  int (*RenderSetClipRect)(void *, const SDL_Rect *);
  int (*GetRendererOutputSize)(void *, int *, int *);
  int (*QueryTexture)(void *, uint32_t *, int *, int *, int *);
} sdl;

static void *overlay_renderer;
static void *overlay_texture;
static int overlay_broken;

static const char *overlay_dir;
static const char *choice_path;
static char **overlay_names;
static int overlay_count;
/* an index into overlay_names, or -1 while disabled */
static int current = -1;
/* the index overlay_texture was made from, -2 when none has been tried */
static int loaded = -2;

/* the pad pico-8 opened, which is the one read for the combo */
static void *controller;
/* the pad button launch.sh named as Menu, or -1 for no quit combo */
static int menu_button = -1;
static int select_held;
/* a direction pressed with Select stays hidden from pico-8 until released */
static int consumed[DIR_COUNT];

static const char *screen_mode_path;
/* an index into screen_modes */
static int screen_mode;
/* the biggest rect pico-8 has drawn to, which is its own standard screen */
static SDL_Rect game_rect;

#define LOG(...) fprintf(stderr, "sdl-display: " __VA_ARGS__)

#define RESOLVE(name) (sdl.name = dlsym(RTLD_NEXT, "SDL_" #name))

static int resolve_sdl(void)
{
  static int resolved;

  if (resolved) {
    return resolved > 0;
  }

  RESOLVE(RenderPresent);
  RESOLVE(DestroyRenderer);
  RESOLVE(PollEvent);
  RESOLVE(GameControllerOpen);
  RESOLVE(GameControllerClose);
  RESOLVE(GameControllerGetButton);
  RESOLVE(PushEvent);
  /* wrapped for every frame pico-8 draws, so it must resolve even if the overlay cannot */
  RESOLVE(RenderCopy);
  /* SDL 2.0.12 and up only, and only cosmetic, so its absence is not fatal */
  RESOLVE(SetTextureScaleMode);

  resolved = RESOLVE(CreateRGBSurfaceFrom) && RESOLVE(CreateTextureFromSurface) &&
                     RESOLVE(DestroyTexture) &&
                     RESOLVE(FreeSurface) &&
                     RESOLVE(SetTextureBlendMode) && RESOLVE(GetError) &&
                     RESOLVE(RenderCopy) && RESOLVE(RenderGetLogicalSize) &&
                     RESOLVE(RenderSetLogicalSize) && RESOLVE(RenderGetViewport) &&
                     RESOLVE(RenderSetViewport) && RESOLVE(RenderGetScale) &&
                     RESOLVE(RenderSetScale) && RESOLVE(RenderIsClipEnabled) &&
                     RESOLVE(RenderGetClipRect) && RESOLVE(RenderSetClipRect) &&
                     RESOLVE(GetRendererOutputSize) && RESOLVE(QueryTexture)
                 ? 1
                 : -1;

  if (resolved < 0) {
    LOG("this SDL2 is missing a renderer function, not drawing an overlay\n");
    overlay_broken = 1;
  }

  return resolved > 0;
}

static int compare_names(const void *a, const void *b)
{
  return strcmp(*(char *const *)a, *(char *const *)b);
}

static void scan_overlays(void)
{
  DIR *dir = opendir(overlay_dir);
  struct dirent *entry;

  if (dir == NULL) {
    LOG("cannot open %s\n", overlay_dir);
    return;
  }

  while ((entry = readdir(dir)) != NULL) {
    const char *name = entry->d_name;
    size_t len = strlen(name);
    char **grown;

    /* the dot check also skips the ._ files macOS leaves on FAT cards */
    if (name[0] == '.' || len < 5 || strcmp(name + len - 4, ".png") != 0) {
      continue;
    }

    grown = realloc(overlay_names, (overlay_count + 1) * sizeof *overlay_names);
    if (grown == NULL) {
      break;
    }
    overlay_names = grown;
    overlay_names[overlay_count] = strdup(name);
    if (overlay_names[overlay_count] != NULL) {
      overlay_count++;
    }
  }

  closedir(dir);
  qsort(overlay_names, overlay_count, sizeof *overlay_names, compare_names);
}

static void read_line(const char *path, char *line, size_t size)
{
  FILE *file = path != NULL ? fopen(path, "r") : NULL;

  line[0] = '\0';
  if (file != NULL) {
    if (fgets(line, size, file) == NULL) {
      line[0] = '\0';
    }
    fclose(file);
  }
  line[strcspn(line, "\r\n")] = '\0';
}

static void write_line(const char *path, const char *line)
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

/* Nothing is drawn until an overlay has been chosen, or after its file is gone. */
static int read_choice(void)
{
  char line[256];
  int i;

  read_line(choice_path, line, sizeof line);
  for (i = 0; i < overlay_count; i++) {
    if (strcmp(overlay_names[i], line) == 0) {
      return i;
    }
  }

  return -1;
}

static int read_screen_mode(void)
{
  char line[64];
  int i;

  read_line(screen_mode_path, line, sizeof line);
  for (i = 0; i < SCREEN_MODE_COUNT; i++) {
    if (strcmp(screen_modes[i], line) == 0) {
      return i;
    }
  }

  return 0;
}

static const char *current_name(void)
{
  return current < 0 ? "disabled" : overlay_names[current];
}

static int read_menu_button(void)
{
  const char *name = getenv("PICO_PAK_MENU_BUTTON");

  if (name != NULL && strcmp(name, "start") == 0) {
    return SDL_CONTROLLER_BUTTON_START;
  }
  if (name != NULL && strcmp(name, "guide") == 0) {
    return SDL_CONTROLLER_BUTTON_GUIDE;
  }

  return -1;
}

static void init_display(void)
{
  static int done;

  if (done) {
    return;
  }
  done = 1;

  menu_button = read_menu_button();
  if (menu_button < 0) {
    LOG("no Menu button named, Select + Menu will not quit pico-8\n");
  }

  screen_mode_path = getenv("PICO_PAK_SCREEN_MODE");
  screen_mode = read_screen_mode();
  LOG("starting in %s screen mode\n", screen_modes[screen_mode]);

  overlay_dir = getenv("PICO_PAK_OVERLAY_DIR");
  choice_path = getenv("PICO_PAK_OVERLAY_CHOICE");
  if (overlay_dir == NULL || *overlay_dir == '\0') {
    LOG("PICO_PAK_OVERLAY_DIR is not set\n");
    return;
  }

  scan_overlays();
  current = read_choice();
  LOG("%d overlays in %s, starting with %s\n", overlay_count, overlay_dir, current_name());
}

/* The disabled state sits after the last overlay, so the cycle wraps through it. */
static void cycle_overlay(int step)
{
  int slots = overlay_count + 1;
  int slot = current < 0 ? overlay_count : current;

  slot = (slot + step + slots) % slots;
  current = slot == overlay_count ? -1 : slot;

  LOG("switched to %s\n", current_name());
  write_line(choice_path, current_name());
}

static void cycle_screen_mode(int step)
{
  screen_mode = (screen_mode + step + SCREEN_MODE_COUNT) % SCREEN_MODE_COUNT;

  LOG("switched to %s screen mode\n", screen_modes[screen_mode]);
  write_line(screen_mode_path, screen_modes[screen_mode]);
}

static void request_quit(void)
{
  /* SDL_Event is a 56 byte union and SDL_QUIT needs only its type */
  uint32_t event[14] = {SDL_QUIT};

  if (sdl.PushEvent == NULL) {
    LOG("this SDL2 has no SDL_PushEvent, cannot quit pico-8\n");
    return;
  }

  LOG("Select + Menu pressed, quitting pico-8\n");
  sdl.PushEvent(event);
}

/*
 * Left and Right are only taken from pico-8 when there are overlays to pick,
 * and Menu only when launch.sh named the button.
 */
static int has_action(int direction)
{
  if (direction == DIR_LEFT || direction == DIR_RIGHT) {
    return overlay_count > 0;
  }
  if (direction == DIR_MENU) {
    return menu_button >= 0;
  }

  return 1;
}

/*
 * Reads Select and the buttons pressed with it straight from the pad. A button
 * counts once per press, and calling this again in the same frame changes nothing.
 */
static void update_combo(void)
{
  static int was_down[DIR_COUNT];
  int buttons[DIR_COUNT];
  int direction, down;

  if (controller == NULL || sdl.GameControllerGetButton == NULL) {
    return;
  }

  buttons[DIR_LEFT] = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
  buttons[DIR_RIGHT] = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
  buttons[DIR_UP] = SDL_CONTROLLER_BUTTON_DPAD_UP;
  buttons[DIR_DOWN] = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
  buttons[DIR_MENU] = menu_button;

  select_held = sdl.GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_BACK);

  for (direction = 0; direction < DIR_COUNT; direction++) {
    down = buttons[direction] >= 0 && sdl.GameControllerGetButton(controller, buttons[direction]);
    if (down && !was_down[direction] && select_held && has_action(direction)) {
      consumed[direction] = 1;
      if (direction == DIR_MENU) {
        request_quit();
      } else if (direction == DIR_LEFT || direction == DIR_RIGHT) {
        cycle_overlay(direction == DIR_RIGHT ? 1 : -1);
      } else {
        cycle_screen_mode(direction == DIR_DOWN ? 1 : -1);
      }
    }
    if (!down) {
      consumed[direction] = 0;
    }
    was_down[direction] = down;
  }
}

static int direction_of(int button)
{
  if (button >= 0 && button == menu_button) {
    return DIR_MENU;
  }

  switch (button) {
  case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
    return DIR_LEFT;
  case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
    return DIR_RIGHT;
  case SDL_CONTROLLER_BUTTON_DPAD_UP:
    return DIR_UP;
  case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
    return DIR_DOWN;
  default:
    return -1;
  }
}

static int is_hidden(int direction)
{
  return direction >= 0 && has_action(direction) && (select_held || consumed[direction]);
}

/* Covers pico-8 reading the d-pad from events as well as from button state. */
static int swallow_event(const void *event)
{
  const SDL_ControllerButtonEvent *button = event;

  if (button->type != SDL_CONTROLLERBUTTONDOWN && button->type != SDL_CONTROLLERBUTTONUP) {
    return 0;
  }

  update_combo();
  return is_hidden(direction_of(button->button));
}

/*
 * Maps a rect pico-8 draws to from its standard screen onto the whole output.
 * The screen is learned as the biggest rect drawn to, which is the game itself.
 */
static int stretch_rect(void *renderer, const SDL_Rect *dst, SDL_Rect *out)
{
  int width, height;

  if ((long)dst->w * dst->h > (long)game_rect.w * game_rect.h) {
    game_rect = *dst;
    LOG("pico-8 draws its screen at %d,%d %dx%d\n", dst->x, dst->y, dst->w, dst->h);
  }

  if (screen_mode != SCREEN_MODE_STRETCHED || game_rect.w <= 0 || game_rect.h <= 0 ||
      sdl.GetRendererOutputSize == NULL ||
      sdl.GetRendererOutputSize(renderer, &width, &height) != 0) {
    return 0;
  }

  out->x = (dst->x - game_rect.x) * width / game_rect.w;
  out->y = (dst->y - game_rect.y) * height / game_rect.h;
  out->w = dst->w * width / game_rect.w;
  out->h = dst->h * height / game_rect.h;
  return 1;
}

/*
 * stb_image hands back RGBA bytes in memory order, which on these little endian
 * devices is the mask set below. SDL copies the pixels into the texture, so both
 * the surface and the pixels are freed as soon as it exists.
 */
static void *load_overlay(void *renderer, const char *name)
{
  char path[4096];
  unsigned char *pixels;
  int width, height;
  void *surface;
  void *texture;

  snprintf(path, sizeof path, "%s/%s", overlay_dir, name);

  pixels = stbi_load(path, &width, &height, NULL, 4);
  if (pixels == NULL) {
    LOG("cannot read %s: %s\n", path, stbi_failure_reason());
    return NULL;
  }

  surface = sdl.CreateRGBSurfaceFrom(pixels, width, height, 32, width * 4, 0x000000ff,
                                     0x0000ff00, 0x00ff0000, 0xff000000);
  if (surface == NULL) {
    LOG("cannot make a surface from %s: %s\n", path, sdl.GetError());
    stbi_image_free(pixels);
    return NULL;
  }

  texture = sdl.CreateTextureFromSurface(renderer, surface);
  sdl.FreeSurface(surface);
  stbi_image_free(pixels);
  if (texture == NULL) {
    LOG("cannot make a texture from %s: %s\n", path, sdl.GetError());
    return NULL;
  }

  sdl.SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
  if (sdl.SetTextureScaleMode != NULL) {
    sdl.SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
  }

  LOG("drawing %s over pico-8\n", path);
  return texture;
}

/*
 * Scales the overlay to fit the screen and centres it, keeping its shape, so a
 * 4:3 bezel on tg5040's 16:9 Smart Pro gets side bars rather than distortion.
 */
static int fit_overlay(void *renderer, SDL_Rect *dst)
{
  int screen_w, screen_h, image_w, image_h;

  if (sdl.GetRendererOutputSize(renderer, &screen_w, &screen_h) != 0 ||
      sdl.QueryTexture(overlay_texture, NULL, NULL, &image_w, &image_h) != 0 ||
      image_w <= 0 || image_h <= 0) {
    return 0;
  }

  if ((long)screen_w * image_h > (long)screen_h * image_w) {
    dst->h = screen_h;
    dst->w = image_w * screen_h / image_h;
  } else {
    dst->w = screen_w;
    dst->h = image_h * screen_w / image_w;
  }
  dst->x = (screen_w - dst->w) / 2;
  dst->y = (screen_h - dst->h) / 2;
  return 1;
}

/*
 * pico-8 may letterbox with a logical size, viewport, scale or clip rect, and
 * any of those would squeeze the overlay into the game's square. They are
 * cleared for the one copy and put back so pico-8's next frame is unaffected.
 */
static void draw_overlay(void *renderer)
{
  int logical_w, logical_h, clip_enabled;
  float scale_x, scale_y;
  SDL_Rect viewport, clip, fitted;

  if (overlay_broken || !resolve_sdl()) {
    return;
  }
  init_display();
  update_combo();

  /* a stretched game fills the screen, so any bezel would cover it */
  if (screen_mode == SCREEN_MODE_STRETCHED) {
    return;
  }

  if (renderer != overlay_renderer) {
    overlay_renderer = renderer;
    overlay_texture = NULL;
    loaded = -2;
  }

  /* a file that will not load is retried only once the choice moves off it */
  if (loaded != current) {
    if (overlay_texture != NULL) {
      sdl.DestroyTexture(overlay_texture);
      overlay_texture = NULL;
    }
    if (current >= 0) {
      overlay_texture = load_overlay(renderer, overlay_names[current]);
    }
    loaded = current;
  }

  if (overlay_texture == NULL) {
    return;
  }

  sdl.RenderGetLogicalSize(renderer, &logical_w, &logical_h);
  sdl.RenderGetViewport(renderer, &viewport);
  sdl.RenderGetScale(renderer, &scale_x, &scale_y);
  clip_enabled = sdl.RenderIsClipEnabled(renderer);
  sdl.RenderGetClipRect(renderer, &clip);

  sdl.RenderSetLogicalSize(renderer, 0, 0);
  sdl.RenderSetScale(renderer, 1.0f, 1.0f);
  sdl.RenderSetViewport(renderer, NULL);
  sdl.RenderSetClipRect(renderer, NULL);

  sdl.RenderCopy(renderer, overlay_texture, NULL, fit_overlay(renderer, &fitted) ? &fitted : NULL);

  if (logical_w > 0 && logical_h > 0) {
    sdl.RenderSetLogicalSize(renderer, logical_w, logical_h);
  }
  sdl.RenderSetScale(renderer, scale_x, scale_y);
  sdl.RenderSetViewport(renderer, &viewport);
  sdl.RenderSetClipRect(renderer, clip_enabled ? &clip : NULL);
}

void SDL_RenderPresent(void *renderer)
{
  resolve_sdl();
  if (sdl.RenderPresent == NULL) {
    return;
  }

  draw_overlay(renderer);
  sdl.RenderPresent(renderer);
}

int SDL_RenderCopy(void *renderer, void *texture, const SDL_Rect *src, const SDL_Rect *dst)
{
  SDL_Rect stretched;

  resolve_sdl();
  if (sdl.RenderCopy == NULL) {
    return -1;
  }

  init_display();
  if (dst != NULL && stretch_rect(renderer, dst, &stretched)) {
    dst = &stretched;
  }

  return sdl.RenderCopy(renderer, texture, src, dst);
}

int SDL_PollEvent(void *event)
{
  int got;

  resolve_sdl();
  if (sdl.PollEvent == NULL) {
    return 0;
  }

  /* a NULL event only asks whether one is pending, so nothing can be dropped */
  if (event == NULL) {
    return sdl.PollEvent(event);
  }

  init_display();
  do {
    got = sdl.PollEvent(event);
  } while (got && swallow_event(event));

  return got;
}

void *SDL_GameControllerOpen(int index)
{
  void *opened;

  resolve_sdl();
  if (sdl.GameControllerOpen == NULL) {
    return NULL;
  }

  opened = sdl.GameControllerOpen(index);
  if (opened != NULL) {
    controller = opened;
    LOG("watching controller %d for Select combos\n", index);
  }

  return opened;
}

void SDL_GameControllerClose(void *closing)
{
  resolve_sdl();

  if (closing == controller) {
    controller = NULL;
  }

  if (sdl.GameControllerClose != NULL) {
    sdl.GameControllerClose(closing);
  }
}

uint8_t SDL_GameControllerGetButton(void *pad, int button)
{
  resolve_sdl();
  if (sdl.GameControllerGetButton == NULL) {
    return 0;
  }

  if (pad == controller && pad != NULL && direction_of(button) >= 0) {
    init_display();
    update_combo();
    if (is_hidden(direction_of(button))) {
      return 0;
    }
  }

  return sdl.GameControllerGetButton(pad, button);
}

/* SDL frees a renderer's textures with it, so a later renderer needs its own. */
void SDL_DestroyRenderer(void *renderer)
{
  resolve_sdl();

  if (renderer == overlay_renderer) {
    overlay_renderer = NULL;
    overlay_texture = NULL;
    loaded = -2;
  }

  if (sdl.DestroyRenderer != NULL) {
    sdl.DestroyRenderer(renderer);
  }
}
