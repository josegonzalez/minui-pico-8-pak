/*
 * Draws a PNG over every frame pico-8 presents, for bezels and overlays.
 *
 * pico-8 has no overlay support of its own, but it draws through SDL's
 * renderer, so wrapping SDL_RenderPresent lets this copy an image over the
 * finished frame just before it reaches the screen. The image is stretched to
 * the whole screen and needs a transparent window where the game shows through.
 *
 * launch.sh only preloads this when the overlay folder holds a PNG, and passes
 * the folder in PICO_PAK_OVERLAY_DIR. Select + Left/Right steps through every
 * PNG there and a disabled state, and the choice is saved by name to the file
 * in PICO_PAK_OVERLAY_CHOICE for the next launch. pico-8 reads the pad with
 * SDL_GameControllerGetButton rather than from events, so the combo is read
 * the same way and hidden from those calls.
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
#define SDL_CONTROLLERBUTTONDOWN 0x650
#define SDL_CONTROLLERBUTTONUP 0x651
#define SDL_CONTROLLER_BUTTON_BACK 4
#define SDL_CONTROLLER_BUTTON_DPAD_LEFT 13
#define SDL_CONTROLLER_BUTTON_DPAD_RIGHT 14

static struct {
  void (*RenderPresent)(void *);
  void (*DestroyRenderer)(void *);
  int (*PollEvent)(void *);
  void *(*GameControllerOpen)(int);
  void (*GameControllerClose)(void *);
  uint8_t (*GameControllerGetButton)(void *, int);
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
static int select_held;
/* a direction pressed with Select stays hidden from pico-8 until released */
static int consumed[2];

#define LOG(...) fprintf(stderr, "sdl-overlay: " __VA_ARGS__)

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
  /* SDL 2.0.12 and up only, and only cosmetic, so its absence is not fatal */
  RESOLVE(SetTextureScaleMode);

  resolved = RESOLVE(CreateTextureFromSurface) && RESOLVE(DestroyTexture) &&
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

/* Nothing is drawn until an overlay has been chosen, or after its file is gone. */
static int read_choice(void)
{
  char line[256] = "";
  FILE *file;
  int i;

  file = choice_path != NULL ? fopen(choice_path, "r") : NULL;
  if (file != NULL) {
    if (fgets(line, sizeof line, file) == NULL) {
      line[0] = '\0';
    }
    fclose(file);
  }
  line[strcspn(line, "\r\n")] = '\0';

  for (i = 0; i < overlay_count; i++) {
    if (strcmp(overlay_names[i], line) == 0) {
      return i;
    }
  }

  return -1;
}

static const char *current_name(void)
{
  return current < 0 ? "disabled" : overlay_names[current];
}

static void save_choice(void)
{
  FILE *file;

  if (choice_path == NULL || *choice_path == '\0') {
    return;
  }

  file = fopen(choice_path, "w");
  if (file == NULL) {
    LOG("cannot save the choice to %s\n", choice_path);
    return;
  }

  fprintf(file, "%s\n", current_name());
  fclose(file);
}

static void init_overlays(void)
{
  static int done;

  if (done) {
    return;
  }
  done = 1;

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
  save_choice();
}

/*
 * Reads Select and the d-pad straight from the pad. A direction counts once per
 * press, and calling this again in the same frame changes nothing.
 */
static void update_combo(void)
{
  static const int directions[2] = {SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT};
  static int was_down[2];
  int side, down;

  if (controller == NULL || sdl.GameControllerGetButton == NULL || overlay_count == 0) {
    return;
  }

  select_held = sdl.GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_BACK);

  for (side = 0; side < 2; side++) {
    down = sdl.GameControllerGetButton(controller, directions[side]);
    if (down && !was_down[side] && select_held) {
      consumed[side] = 1;
      cycle_overlay(side ? 1 : -1);
    }
    if (!down) {
      consumed[side] = 0;
    }
    was_down[side] = down;
  }
}

static int direction_side(int button)
{
  if (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
    return 0;
  }
  if (button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
    return 1;
  }
  return -1;
}

static int is_hidden(int side)
{
  return side >= 0 && (select_held || consumed[side]);
}

/* Covers pico-8 reading the d-pad from events as well as from button state. */
static int swallow_event(const void *event)
{
  const SDL_ControllerButtonEvent *button = event;

  if (button->type != SDL_CONTROLLERBUTTONDOWN && button->type != SDL_CONTROLLERBUTTONUP) {
    return 0;
  }

  update_combo();
  return is_hidden(direction_side(button->button));
}

static void *load_overlay(void *renderer, const char *name)
{
  static void *(*img_load)(const char *);
  char path[4096];
  void *surface;
  void *texture;

  if (img_load == NULL) {
    /* found through the LD_LIBRARY_PATH launch.sh gives pico-8 */
    void *image_lib = dlopen("libSDL2_image-2.0.so.0", RTLD_NOW);

    if (image_lib == NULL) {
      LOG("cannot load SDL2_image: %s\n", dlerror());
      overlay_broken = 1;
      return NULL;
    }

    img_load = (void *(*)(const char *))dlsym(image_lib, "IMG_Load");
    if (img_load == NULL) {
      LOG("SDL2_image has no IMG_Load\n");
      overlay_broken = 1;
      return NULL;
    }
  }

  snprintf(path, sizeof path, "%s/%s", overlay_dir, name);

  surface = img_load(path);
  if (surface == NULL) {
    LOG("cannot read %s: %s\n", path, sdl.GetError());
    return NULL;
  }

  texture = sdl.CreateTextureFromSurface(renderer, surface);
  sdl.FreeSurface(surface);
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
  init_overlays();
  update_combo();

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

int SDL_PollEvent(void *event)
{
  int got;

  resolve_sdl();
  if (sdl.PollEvent == NULL) {
    return 0;
  }

  /* a NULL event only asks whether one is pending, so nothing can be dropped */
  if (event == NULL || overlay_broken) {
    return sdl.PollEvent(event);
  }

  init_overlays();
  do {
    got = sdl.PollEvent(event);
  } while (got && overlay_count > 0 && swallow_event(event));

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
  if (opened != NULL && !overlay_broken) {
    controller = opened;
    LOG("watching controller %d for Select + Left/Right\n", index);
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

  if (pad == controller && pad != NULL && direction_side(button) >= 0) {
    init_overlays();
    update_combo();
    if (overlay_count > 0 && is_hidden(direction_side(button))) {
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
