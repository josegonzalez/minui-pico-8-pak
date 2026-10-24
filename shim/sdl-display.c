/*
 * Draws a PNG over every frame pico-8 presents, for bezels and overlays, and
 * opens minarch's in-game menu when Menu is tapped.
 *
 * pico-8 has no overlay support of its own, but it draws through SDL's
 * renderer, so wrapping SDL_RenderPresent lets this copy an image over the
 * finished frame just before it reaches the screen. The image is scaled to fit
 * the screen without changing its shape and needs a transparent window where
 * the game shows through. The PNG is decoded here with stb_image, so overlays
 * need nothing on the device beyond the SDL2 pico-8 already uses.
 *
 * Every controller mapping in controllers/ reports Menu as the guide button,
 * which is hidden from pico-8. Releasing it, unless another button or a volume
 * key was pressed with it, opens the menu from SDL_RenderPresent: the finished
 * frame is read back, dimmed and drawn under the menu, and pico-8's main thread
 * waits there with its audio paused until the menu closes, which pauses the
 * cart. menu.c holds the pages and gfx.c the drawing. Quit fades the menu to
 * black, and every frame pico-8 presents until it exits is black, so the
 * screen stays dark while it and launch.sh finish rather than showing the game.
 *
 * Nothing else draws the volume and brightness keymon changes over pico-8, so
 * this reads them from the launcher's own libmsettings and draws minarch's
 * indicator over the game, or in the menu in place of the battery, while they
 * change.
 *
 * The menu's Options page sets the overlay, from the PNGs in
 * PICO_PAK_OVERLAY_DIR, and the screen mode, between standard and stretched.
 * pico-8 only takes its draw rect at startup, so launch.sh always starts it in
 * standard mode and stretching is done here by redirecting the copy pico-8
 * draws each frame with. launch.sh names every file the menu saves to in the
 * PICO_PAK_ variables read by init_display.
 *
 * Every SDL call goes through dlsym, like sdl-nosensor.c, so this builds without
 * SDL headers and asks glibc for nothing newer than that shim does. See
 * shim/Dockerfile.
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gfx.h"
#include "menu.h"

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

/* From SDL_rect.h, SDL_blendmode.h, SDL_events.h, SDL_pixels.h, SDL_render.h,
 * SDL_audio.h and SDL_gamecontroller.h. */
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
#define SDL_PIXELFORMAT_ARGB8888 0x16362004
#define SDL_TEXTUREACCESS_STREAMING 1
#define SDL_AUDIO_PLAYING 1
#define SDL_QUIT 0x100
#define SDL_CONTROLLERBUTTONDOWN 0x650
#define SDL_CONTROLLERBUTTONUP 0x651
#define SDL_CONTROLLER_BUTTON_A 0
#define SDL_CONTROLLER_BUTTON_B 1
#define SDL_CONTROLLER_BUTTON_GUIDE 5
#define SDL_CONTROLLER_BUTTON_DPAD_UP 11
#define SDL_CONTROLLER_BUTTON_DPAD_DOWN 12
#define SDL_CONTROLLER_BUTTON_DPAD_LEFT 13
#define SDL_CONTROLLER_BUTTON_DPAD_RIGHT 14
#define SDL_CONTROLLER_BUTTON_MAX 21

/* the most input devices watched for the volume keys */
#define INPUT_DEVICES 16
/* how long the volume or brightness stays on screen after it last changed, as in minarch */
#define INDICATOR_MS 500
/* the steps the menu fades to black in after Quit, and how long each lasts */
#define FADE_STEPS 8
#define FADE_STEP_MS 20

/*
 * The functions of the launcher's libmsettings the indicator reads. Every
 * MinUI and NextUI build has the first three, and GetMute is NextUI's.
 */
static struct {
  void (*InitSettings)(void);
  int (*GetVolume)(void);
  int (*GetBrightness)(void);
  int (*GetMute)(void);
} msettings;

static struct {
  void (*RenderPresent)(void *);
  void (*DestroyRenderer)(void *);
  int (*PollEvent)(void *);
  int (*PushEvent)(void *);
  void *(*GameControllerOpen)(int);
  void (*GameControllerClose)(void *);
  uint8_t (*GameControllerGetButton)(void *, int);
  void *(*GameControllerGetJoystick)(void *);
  int (*JoystickNumButtons)(void *);
  uint8_t (*JoystickGetButton)(void *, int);
  void *(*CreateRGBSurfaceFrom)(void *, int, int, int, int, uint32_t, uint32_t, uint32_t,
                                uint32_t);
  void *(*CreateTextureFromSurface)(void *, void *);
  void *(*CreateTexture)(void *, uint32_t, int, int, int);
  int (*UpdateTexture)(void *, const SDL_Rect *, const void *, int);
  void (*DestroyTexture)(void *);
  void (*FreeSurface)(void *);
  int (*SetTextureBlendMode)(void *, int);
  int (*SetTextureScaleMode)(void *, int);
  const char *(*GetError)(void);
  int (*RenderCopy)(void *, void *, const SDL_Rect *, const SDL_Rect *);
  int (*RenderClear)(void *);
  int (*RenderGetDrawColor)(void *, uint8_t *, uint8_t *, uint8_t *, uint8_t *);
  int (*RenderSetDrawColor)(void *, uint8_t, uint8_t, uint8_t, uint8_t);
  int (*RenderReadPixels)(void *, const SDL_Rect *, uint32_t, void *, int);
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
  void (*Delay)(uint32_t);
  uint32_t (*GetTicks)(void);
  uint32_t (*OpenAudioDevice)(const char *, int, const void *, void *, int);
  int (*OpenAudio)(void *, void *);
  void (*PauseAudioDevice)(uint32_t, int);
  int (*GetAudioDeviceStatus)(uint32_t);
} sdl;

static void *overlay_renderer;
static void *overlay_texture;
static int overlay_broken;
static int menu_broken;

static const char *overlay_dir;
static char **overlay_names;
static int overlay_count;
/* the index overlay_texture was made from, -2 when none has been tried */
static int loaded = -2;

/* the overlay, screen mode and files the menu changes, read by init_display */
static struct menu_settings settings = {.overlay = -1};

/* the pad pico-8 opened, which is the one the menu reads */
static void *controller;
/* a Menu release opens the menu from the next SDL_RenderPresent */
static int menu_requested;
static int menu_was_down;
/* another button or a volume key went with this press of Menu */
static int menu_spoiled;
/* buttons still held from the menu, hidden from pico-8 until released */
static uint8_t hidden[SDL_CONTROLLER_BUTTON_MAX];
static int hidden_count;

static int input_fds[INPUT_DEVICES];
static int input_count;

/* the setting shown in the top right while it changes, and its value */
static enum menu_indicator indicator;
static int indicator_value;
static uint32_t indicator_at;
/* the settings as last read, -1 before the first read */
static int last_volume = -1, last_brightness = -1, last_mute = -1;
/* the texture the game's indicator is drawn from, and what it last showed */
static void *indicator_renderer;
static void *indicator_texture;
static enum menu_indicator indicator_drawn;
static int indicator_drawn_value;
static uint32_t *indicator_pixels;

/* set once Quit is chosen, so every frame until pico-8 exits is black */
static int quitting;

/* the device pico-8 plays through, paused while the menu is open */
static uint32_t audio_device;

static const char *cart_name;
static const char *power_supply_dir;
static gfx_font *menu_font;
static gfx_theme menu_theme;
static uint32_t *menu_frame;
static uint32_t *menu_pixels;
static size_t menu_buffer_size;

/* the biggest rect pico-8 has drawn to, which is its own standard screen */
static SDL_Rect game_rect;

#define LOG(...) gfx_log(__VA_ARGS__)

/*
 * The shim is built with hidden visibility, so menu.c and gfx.c can never bind
 * to a function of the same name in pico-8, and only the wrappers are exported.
 */
#define EXPORT __attribute__((visibility("default")))

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
  /* wrapped, so they must resolve even if the menu cannot */
  RESOLVE(OpenAudioDevice);
  RESOLVE(OpenAudio);
  /* only used to tell a tap of Menu from a chord, which pad buttons can also do */
  RESOLVE(GameControllerGetJoystick);
  RESOLVE(JoystickNumButtons);
  RESOLVE(JoystickGetButton);
  /* without these the menu still opens, over a game whose sound keeps playing */
  RESOLVE(PauseAudioDevice);
  RESOLVE(GetAudioDeviceStatus);
  /* without these the frames after Quit show the game rather than black */
  RESOLVE(RenderGetDrawColor);
  RESOLVE(RenderSetDrawColor);

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
    LOG("this SDL2 is missing a renderer function, not drawing an overlay or the menu\n");
    overlay_broken = 1;
    menu_broken = 1;
  } else if (!(RESOLVE(CreateTexture) && RESOLVE(UpdateTexture) && RESOLVE(RenderClear) &&
               RESOLVE(RenderReadPixels) && RESOLVE(Delay) && RESOLVE(GetTicks) &&
               RESOLVE(GameControllerGetButton) && RESOLVE(PollEvent) &&
               RESOLVE(RenderPresent))) {
    LOG("this SDL2 is missing a function the menu needs, Menu goes to pico-8\n");
    menu_broken = 1;
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

static const char *current_name(void)
{
  return settings.overlay < 0 ? "disabled" : overlay_names[settings.overlay];
}

/*
 * Every event device is watched for the volume keys, which the OS pairs with
 * Menu for brightness and SDL never sees. Opened read-only and unexclusive, so
 * the OS still reads every key itself.
 */
static void open_input_devices(void)
{
  const char *input_dir = getenv("PICO_PAK_INPUT_DIR");
  char path[4096];
  struct dirent *entry;
  DIR *dir;
  int fd;

  if (input_dir == NULL || *input_dir == '\0') {
    input_dir = "/dev/input";
  }

  dir = opendir(input_dir);
  if (dir == NULL) {
    LOG("cannot open %s, Menu + a volume key may open the menu\n", input_dir);
    return;
  }

  while ((entry = readdir(dir)) != NULL && input_count < INPUT_DEVICES) {
    if (strncmp(entry->d_name, "event", 5) != 0) {
      continue;
    }
    snprintf(path, sizeof path, "%s/%s", input_dir, entry->d_name);
    fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd >= 0) {
      input_fds[input_count++] = fd;
    }
  }

  closedir(dir);
}

/*
 * Reads everything waiting on the input devices and returns how many volume
 * key presses and repeats there have been so far. Both Menu, which a volume
 * key turns into a brightness chord, and the indicator, which shows a key
 * pressed at the end of its range, keep their own mark of it to compare with.
 */
static unsigned volume_key_presses(void)
{
  static unsigned presses;
  struct input_event events[16];
  ssize_t got;
  int i;
  size_t e;

  for (i = 0; i < input_count; i++) {
    while ((got = read(input_fds[i], events, sizeof events)) > 0) {
      for (e = 0; e < (size_t)got / sizeof *events; e++) {
        if (events[e].type == EV_KEY && events[e].value != 0 &&
            (events[e].code == KEY_VOLUMEUP || events[e].code == KEY_VOLUMEDOWN)) {
          presses++;
        }
      }
    }
  }

  return presses;
}

/*
 * The launcher's own settings library, for the volume and brightness its
 * keymon changes. It is only opened when keymon has already shared the
 * settings, as a process that opens them first becomes their owner.
 */
static void open_msettings(void)
{
  const char *shared = getenv("PICO_PAK_SHARED_SETTINGS");
  const char *library = getenv("PICO_PAK_MSETTINGS");
  void *handle;

  if (shared == NULL || *shared == '\0') {
    shared = "/dev/shm/SharedSettings";
  }
  if (library == NULL || *library == '\0') {
    library = "libmsettings.so";
  }

  if (access(shared, F_OK) != 0) {
    LOG("no settings shared at %s, not showing volume or brightness changes\n", shared);
    return;
  }

  handle = dlopen(library, RTLD_NOW | RTLD_LOCAL);
  if (handle == NULL) {
    LOG("cannot open %s, not showing volume or brightness changes: %s\n", library, dlerror());
    return;
  }

  msettings.InitSettings = (void (*)(void))dlsym(handle, "InitSettings");
  msettings.GetVolume = (int (*)(void))dlsym(handle, "GetVolume");
  msettings.GetBrightness = (int (*)(void))dlsym(handle, "GetBrightness");
  msettings.GetMute = (int (*)(void))dlsym(handle, "GetMute");
  if (msettings.InitSettings == NULL || msettings.GetVolume == NULL ||
      msettings.GetBrightness == NULL) {
    LOG("%s is missing a function, not showing volume or brightness changes\n", library);
    msettings.GetVolume = NULL;
    return;
  }

  msettings.InitSettings();
  LOG("showing volume and brightness changes from %s\n", library);
}

/*
 * Shows whichever of volume and brightness changed since the last frame, or
 * the one a volume key is changing when the value is already at its end, and
 * hides it once nothing has changed for a while, as minarch does. A volume key
 * pressed with Menu changes the brightness.
 */
static int update_indicator(uint32_t now, int menu_held)
{
  static unsigned seen_presses;
  enum menu_indicator was = indicator;
  int was_value = indicator_value;
  unsigned presses = volume_key_presses();
  int volume, brightness, mute;

  if (msettings.GetVolume == NULL) {
    seen_presses = presses;
    return 0;
  }

  volume = msettings.GetVolume();
  brightness = msettings.GetBrightness();
  mute = msettings.GetMute != NULL ? msettings.GetMute() : 0;

  if (last_brightness >= 0 && brightness != last_brightness) {
    indicator = MENU_INDICATOR_BRIGHTNESS;
    indicator_at = now;
  } else if (last_volume >= 0 && (volume != last_volume || mute != last_mute)) {
    indicator = MENU_INDICATOR_VOLUME;
    indicator_at = now;
  } else if (presses != seen_presses) {
    indicator = menu_held ? MENU_INDICATOR_BRIGHTNESS : MENU_INDICATOR_VOLUME;
    indicator_at = now;
  } else if (indicator != MENU_INDICATOR_NONE && now - indicator_at >= INDICATOR_MS) {
    indicator = MENU_INDICATOR_NONE;
  }

  last_volume = volume;
  last_brightness = brightness;
  last_mute = mute;
  seen_presses = presses;
  indicator_value = indicator == MENU_INDICATOR_BRIGHTNESS ? brightness : volume;
  return indicator != was || indicator_value != was_value;
}

static void init_display(void)
{
  static int done;

  if (done) {
    return;
  }
  done = 1;

  settings.screen_mode_path = getenv("PICO_PAK_SCREEN_MODE");
  settings.screen_mode = menu_read_screen_mode(settings.screen_mode_path);
  LOG("starting in %s screen mode\n", menu_screen_modes[settings.screen_mode]);

  settings.overlay_path = getenv("PICO_PAK_OVERLAY_CHOICE");
  settings.copy_carts_dir = getenv("PICO_PAK_COPY_CARTS_DIR");
  settings.shared_copy_carts_dir = getenv("PICO_PAK_SHARED_COPY_CARTS_DIR");
  settings.splore_artwork_path = getenv("PICO_PAK_SPLORE_ARTWORK");
  settings.pico8_config_path = getenv("PICO_PAK_PICO8_CONFIG");
  settings.installed_config_path = getenv("PICO_PAK_INSTALLED_CONFIG");
  cart_name = getenv("PICO_PAK_CART_NAME");
  power_supply_dir = getenv("PICO_PAK_POWER_SUPPLY_DIR");
  if (power_supply_dir == NULL || *power_supply_dir == '\0') {
    power_supply_dir = "/sys/class/power_supply";
  }
  open_input_devices();
  open_msettings();

  overlay_dir = getenv("PICO_PAK_OVERLAY_DIR");
  if (overlay_dir == NULL || *overlay_dir == '\0') {
    LOG("PICO_PAK_OVERLAY_DIR is not set\n");
    return;
  }

  scan_overlays();
  settings.overlay_names = overlay_names;
  settings.overlay_count = overlay_count;
  settings.overlay = menu_read_overlay(settings.overlay_path, overlay_names, overlay_count);
  LOG("%d overlays in %s, starting with %s\n", overlay_count, overlay_dir, current_name());
}

static void request_quit(void)
{
  /* SDL_Event is a 56 byte union and SDL_QUIT needs only its type */
  uint32_t event[14] = {SDL_QUIT};

  if (sdl.PushEvent == NULL) {
    LOG("this SDL2 has no SDL_PushEvent, cannot quit pico-8\n");
    return;
  }

  LOG("Quit chosen in the menu, quitting pico-8\n");
  sdl.PushEvent(event);
}

static int menu_available(void)
{
  return resolve_sdl() && !menu_broken && controller != NULL;
}

static uint8_t pad_button(int button)
{
  return sdl.GameControllerGetButton(controller, button);
}

/*
 * A Menu held with any other button is a chord for the OS, such as
 * brightness on rg35xxplus, whose volume keys are pad buttons no mapping
 * names, so the raw buttons are counted rather than the mapped ones.
 */
static int other_buttons_down(void)
{
  void *joystick;
  int count, i, down = 0;

  joystick = sdl.GameControllerGetJoystick != NULL && sdl.JoystickNumButtons != NULL &&
                     sdl.JoystickGetButton != NULL
                 ? sdl.GameControllerGetJoystick(controller)
                 : NULL;
  if (joystick != NULL) {
    count = sdl.JoystickNumButtons(joystick);
    for (i = 0; i < count; i++) {
      down += sdl.JoystickGetButton(joystick, i) != 0;
    }
    return down > 1;
  }

  for (i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++) {
    if (i != SDL_CONTROLLER_BUTTON_GUIDE && pad_button(i)) {
      return 1;
    }
  }
  return 0;
}

/*
 * Reads Menu straight from the pad and asks for the menu when a press ends
 * without being part of a chord. Calling this again in the same frame changes
 * nothing. Buttons hidden since the menu closed come back once released.
 */
static void update_buttons(void)
{
  static unsigned presses_at_press;
  int down, button;

  if (!menu_available()) {
    return;
  }

  down = pad_button(SDL_CONTROLLER_BUTTON_GUIDE);
  if (down && !menu_was_down) {
    /* only a volume key pressed from here on counts against this press */
    presses_at_press = volume_key_presses();
    menu_spoiled = 0;
  }
  if (down && !menu_spoiled &&
      (volume_key_presses() != presses_at_press || other_buttons_down())) {
    LOG("Menu pressed with another key, not opening the menu\n");
    menu_spoiled = 1;
  }
  /* a volume key read only now still went with this press */
  if (!down && menu_was_down && !menu_spoiled && volume_key_presses() == presses_at_press) {
    menu_requested = 1;
  }
  menu_was_down = down;

  for (button = 0; hidden_count > 0 && button < SDL_CONTROLLER_BUTTON_MAX; button++) {
    if (hidden[button] && !pad_button(button)) {
      hidden[button] = 0;
      hidden_count--;
    }
  }
}

static int is_hidden(int button)
{
  if (!menu_available() || button < 0 || button >= SDL_CONTROLLER_BUTTON_MAX) {
    return 0;
  }

  return button == SDL_CONTROLLER_BUTTON_GUIDE || hidden[button];
}

/* Covers pico-8 reading the pad from events as well as from button state. */
static int swallow_event(const void *event)
{
  const SDL_ControllerButtonEvent *button = event;

  if (button->type != SDL_CONTROLLERBUTTONDOWN && button->type != SDL_CONTROLLERBUTTONUP) {
    return 0;
  }

  update_buttons();
  return is_hidden(button->button);
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

  if (settings.screen_mode != MENU_SCREEN_MODE_STRETCHED || game_rect.w <= 0 ||
      game_rect.h <= 0 || sdl.GetRendererOutputSize == NULL ||
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

struct letterbox {
  int logical_w, logical_h, clip_enabled;
  float scale_x, scale_y;
  SDL_Rect viewport, clip;
};

/*
 * pico-8 may letterbox with a logical size, viewport, scale or clip rect, and
 * any of those would squeeze the overlay or the menu into the game's square.
 * They are cleared for what the shim draws and put back so pico-8's next frame
 * is unaffected.
 */
static void clear_letterbox(void *renderer, struct letterbox *saved)
{
  sdl.RenderGetLogicalSize(renderer, &saved->logical_w, &saved->logical_h);
  sdl.RenderGetViewport(renderer, &saved->viewport);
  sdl.RenderGetScale(renderer, &saved->scale_x, &saved->scale_y);
  saved->clip_enabled = sdl.RenderIsClipEnabled(renderer);
  sdl.RenderGetClipRect(renderer, &saved->clip);

  sdl.RenderSetLogicalSize(renderer, 0, 0);
  sdl.RenderSetScale(renderer, 1.0f, 1.0f);
  sdl.RenderSetViewport(renderer, NULL);
  sdl.RenderSetClipRect(renderer, NULL);
}

static void restore_letterbox(void *renderer, const struct letterbox *saved)
{
  if (saved->logical_w > 0 && saved->logical_h > 0) {
    sdl.RenderSetLogicalSize(renderer, saved->logical_w, saved->logical_h);
  }
  sdl.RenderSetScale(renderer, saved->scale_x, saved->scale_y);
  sdl.RenderSetViewport(renderer, &saved->viewport);
  sdl.RenderSetClipRect(renderer, saved->clip_enabled ? &saved->clip : NULL);
}

static void draw_overlay(void *renderer)
{
  struct letterbox saved;
  SDL_Rect fitted;

  if (overlay_broken || !resolve_sdl()) {
    return;
  }

  /* a stretched game fills the screen, so any bezel would cover it */
  if (settings.screen_mode == MENU_SCREEN_MODE_STRETCHED) {
    return;
  }

  if (renderer != overlay_renderer) {
    overlay_renderer = renderer;
    overlay_texture = NULL;
    loaded = -2;
  }

  /* a file that will not load is retried only once the choice moves off it */
  if (loaded != settings.overlay) {
    if (overlay_texture != NULL) {
      sdl.DestroyTexture(overlay_texture);
      overlay_texture = NULL;
    }
    if (settings.overlay >= 0) {
      overlay_texture = load_overlay(renderer, overlay_names[settings.overlay]);
    }
    loaded = settings.overlay;
  }

  if (overlay_texture == NULL) {
    return;
  }

  clear_letterbox(renderer, &saved);
  sdl.RenderCopy(renderer, overlay_texture, NULL, fit_overlay(renderer, &fitted) ? &fitted : NULL);
  restore_letterbox(renderer, &saved);
}

/* the menu's buttons, read straight from the pad */
static unsigned read_menu_buttons(void)
{
  static const struct {
    int button;
    unsigned bit;
  } map[] = {
      {SDL_CONTROLLER_BUTTON_DPAD_UP, MENU_UP},     {SDL_CONTROLLER_BUTTON_DPAD_DOWN, MENU_DOWN},
      {SDL_CONTROLLER_BUTTON_DPAD_LEFT, MENU_LEFT}, {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, MENU_RIGHT},
      {SDL_CONTROLLER_BUTTON_A, MENU_A},            {SDL_CONTROLLER_BUTTON_B, MENU_B},
      {SDL_CONTROLLER_BUTTON_GUIDE, MENU_MENU},
  };
  unsigned held = 0;
  size_t i;

  for (i = 0; i < sizeof map / sizeof *map; i++) {
    if (pad_button(map[i].button)) {
      held |= map[i].bit;
    }
  }

  return held;
}

/* pausing a device pico-8 paused itself would have the menu start it again */
static int pause_audio(void)
{
  if (audio_device == 0 || sdl.PauseAudioDevice == NULL ||
      (sdl.GetAudioDeviceStatus != NULL &&
       sdl.GetAudioDeviceStatus(audio_device) != SDL_AUDIO_PLAYING)) {
    return 0;
  }

  sdl.PauseAudioDevice(audio_device, 1);
  return 1;
}

static int make_menu_buffers(int width, int height)
{
  size_t size = (size_t)width * height * sizeof *menu_pixels;
  uint32_t *frame, *pixels;

  if (size <= menu_buffer_size) {
    return 1;
  }

  frame = realloc(menu_frame, size);
  if (frame != NULL) {
    menu_frame = frame;
  }
  pixels = realloc(menu_pixels, size);
  if (pixels != NULL) {
    menu_pixels = pixels;
  }
  if (frame == NULL || pixels == NULL) {
    LOG("cannot allocate the menu for a %dx%d screen\n", width, height);
    return 0;
  }

  menu_buffer_size = size;
  return 1;
}

static void show_pixels(void *renderer, void *texture, const uint32_t *pixels, int width)
{
  sdl.UpdateTexture(texture, NULL, pixels, width * (int)sizeof *pixels);
  sdl.RenderClear(renderer);
  sdl.RenderCopy(renderer, texture, NULL, NULL);
}

/* the launcher's font and look, read the first time the menu or indicator draws */
static void load_menu_look(void)
{
  if (menu_font == NULL) {
    menu_font = gfx_font_open(getenv("PICO_PAK_MENU_FONT"));
    gfx_theme_init(&menu_theme, getenv("PICO_PAK_MENU_STYLE"), getenv("PICO_PAK_MENU_COLORS"));
  }
}

/*
 * Draws the volume or brightness indicator over the game at the top right, as
 * minarch does, from a texture redrawn only when what it shows changes.
 */
static void draw_indicator(void *renderer)
{
  static int texture_w, texture_h;
  struct letterbox saved;
  gfx_canvas canvas;
  gfx_metrics m;
  SDL_Rect dst;
  int width, height, w, h;
  uint32_t *pixels;

  if (indicator == MENU_INDICATOR_NONE || overlay_broken || menu_broken ||
      sdl.GetRendererOutputSize(renderer, &width, &height) != 0) {
    return;
  }

  menu_indicator_size(width, height, &w, &h);
  if (renderer != indicator_renderer || w != texture_w || h != texture_h) {
    if (indicator_texture != NULL && renderer == indicator_renderer) {
      sdl.DestroyTexture(indicator_texture);
    }
    indicator_renderer = renderer;
    indicator_texture = NULL;
    pixels = realloc(indicator_pixels, (size_t)w * h * sizeof *pixels);
    if (pixels == NULL) {
      return;
    }
    indicator_pixels = pixels;
    indicator_texture = sdl.CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_STREAMING, w, h);
    if (indicator_texture == NULL) {
      LOG("cannot make a texture for the volume indicator: %s\n", sdl.GetError());
      return;
    }
    sdl.SetTextureBlendMode(indicator_texture, SDL_BLENDMODE_BLEND);
    texture_w = w;
    texture_h = h;
    indicator_drawn = MENU_INDICATOR_NONE;
  }
  if (indicator_texture == NULL) {
    return;
  }

  if (indicator != indicator_drawn || indicator_value != indicator_drawn_value) {
    load_menu_look();
    canvas.pixels = indicator_pixels;
    canvas.w = w;
    canvas.h = h;
    /* clear rather than black, so the pill's rounded ends show the game */
    gfx_clear(&canvas, 0);
    menu_draw_indicator(&canvas, width, height, &menu_theme, 0, 0, indicator, indicator_value);
    sdl.UpdateTexture(indicator_texture, NULL, indicator_pixels, w * (int)sizeof *indicator_pixels);
    indicator_drawn = indicator;
    indicator_drawn_value = indicator_value;
  }

  m = gfx_metrics_for(width, height);
  dst.x = width - gfx_u(&m, m.padding) - w;
  dst.y = gfx_u(&m, m.padding);
  dst.w = w;
  dst.h = h;
  clear_letterbox(renderer, &saved);
  sdl.RenderCopy(renderer, indicator_texture, NULL, &dst);
  restore_letterbox(renderer, &saved);
}

/* clears the whole screen to black, putting pico-8's draw colour back after */
static void clear_to_black(void *renderer)
{
  struct letterbox saved;
  uint8_t r, g, b, a;

  if (overlay_broken || sdl.RenderClear == NULL || sdl.RenderGetDrawColor == NULL ||
      sdl.RenderSetDrawColor == NULL) {
    return;
  }

  clear_letterbox(renderer, &saved);
  sdl.RenderGetDrawColor(renderer, &r, &g, &b, &a);
  sdl.RenderSetDrawColor(renderer, 0, 0, 0, 0xff);
  sdl.RenderClear(renderer);
  sdl.RenderSetDrawColor(renderer, r, g, b, a);
  restore_letterbox(renderer, &saved);
}

/*
 * After Quit, pico-8 still has to notice the request and shut down, and
 * launch.sh still has work to do before the launcher draws again, so the
 * screen fades from the menu to black rather than going back to the game.
 */
static void fade_out(void *renderer, void *texture, gfx_canvas *canvas, int drawn)
{
  size_t count = (size_t)canvas->w * canvas->h;
  int step;

  /* fade from what is on screen, which is the dimmed game if the menu never drew */
  if (drawn) {
    memcpy(menu_frame, canvas->pixels, count * sizeof *menu_frame);
  } else {
    gfx_dim(canvas, menu_frame, 0x80);
    memcpy(menu_frame, canvas->pixels, count * sizeof *menu_frame);
  }

  for (step = 1; step <= FADE_STEPS; step++) {
    gfx_dim(canvas, menu_frame, 255 * step / FADE_STEPS);
    show_pixels(renderer, texture, canvas->pixels, canvas->w);
    sdl.RenderPresent(renderer);
    sdl.Delay(FADE_STEP_MS);
  }
}

/*
 * The cart waits here, in the middle of presenting a frame, until the menu
 * closes. The frame it was presenting is drawn again on the way out, for the
 * SDL_RenderPresent the menu was opened from to show, or black after Quit.
 */
static void run_menu(void *renderer)
{
  enum menu_result result = MENU_STAY;
  struct letterbox saved;
  struct menu menu;
  gfx_canvas canvas;
  gfx_battery battery;
  uint32_t event[14];
  unsigned held, was_held, pressed, presses_at_menu = 0;
  int width, height, paused, button, drawn = 0, menu_chord = 0;
  size_t i;
  void *texture;

  if (sdl.GetRendererOutputSize(renderer, &width, &height) != 0 || width <= 0 || height <= 0) {
    LOG("cannot read the screen size, not opening the menu: %s\n", sdl.GetError());
    return;
  }
  if (!make_menu_buffers(width, height)) {
    return;
  }

  texture = sdl.CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                              width, height);
  if (texture == NULL) {
    LOG("cannot make a texture for the menu: %s\n", sdl.GetError());
    return;
  }

  clear_letterbox(renderer, &saved);
  if (sdl.RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, menu_frame,
                           width * (int)sizeof *menu_frame) != 0) {
    LOG("cannot read the game's frame, drawing the menu over black: %s\n", sdl.GetError());
    memset(menu_frame, 0, (size_t)width * height * sizeof *menu_frame);
  }
  /* the back buffer's alpha means nothing, and the frame is drawn opaque */
  for (i = 0; i < (size_t)width * height; i++) {
    menu_frame[i] |= 0xff000000u;
  }

  load_menu_look();
  paused = pause_audio();
  LOG("opening the menu%s\n", paused ? " and pausing pico-8's audio" : "");
  menu_open(&menu, &settings, cart_name);
  menu.indicator = indicator;
  menu.indicator_value = indicator_value;
  battery = gfx_read_battery(power_supply_dir);
  canvas.pixels = menu_pixels;
  canvas.w = width;
  canvas.h = height;

  /* a button already down when the menu opens is not a press */
  was_held = read_menu_buttons();
  while (result == MENU_STAY) {
    while (sdl.PollEvent(event)) {
      if (event[0] == SDL_QUIT) {
        result = MENU_QUIT;
      }
    }
    if (result != MENU_STAY) {
      break;
    }

    /*
     * Menu closes the menu when it is let go, as it opened it, unless it was
     * held for the brightness or with another button, as in the game
     */
    held = read_menu_buttons();
    pressed = held & ~was_held & ~(unsigned)MENU_MENU;
    if (held & MENU_MENU) {
      if (!(was_held & MENU_MENU)) {
        presses_at_menu = volume_key_presses();
        menu_chord = 0;
      }
      if (volume_key_presses() != presses_at_menu || other_buttons_down()) {
        menu_chord = 1;
      }
    } else if ((was_held & MENU_MENU) && !menu_chord) {
      pressed |= MENU_MENU;
    }

    if (update_indicator(sdl.GetTicks(), (held & MENU_MENU) != 0)) {
      menu.indicator = indicator;
      menu.indicator_value = indicator_value;
      menu.dirty = 1;
    }

    result = menu_update(&menu, pressed, held, sdl.GetTicks());
    was_held = held;

    if (menu_font != NULL && menu.dirty && result != MENU_QUIT) {
      menu_draw(&menu, &canvas, menu_frame, menu_font, &menu_theme, &battery);
      show_pixels(renderer, texture, menu_pixels, width);
      sdl.RenderPresent(renderer);
      drawn = 1;
    }
    if (result == MENU_STAY) {
      sdl.Delay(16);
    }
  }

  if (result == MENU_QUIT) {
    /* the cart's sound stays paused, and every frame left is black */
    fade_out(renderer, texture, &canvas, drawn);
    quitting = 1;
    LOG("faded out, every frame is black until pico-8 exits\n");
  } else {
    show_pixels(renderer, texture, menu_frame, width);
    if (paused) {
      sdl.PauseAudioDevice(audio_device, 0);
    }
  }
  sdl.DestroyTexture(texture);
  restore_letterbox(renderer, &saved);

  /* the press that closed the menu is not the cart's */
  for (button = 0; button < SDL_CONTROLLER_BUTTON_MAX; button++) {
    if (!hidden[button] && pad_button(button)) {
      hidden[button] = 1;
      hidden_count++;
    }
  }
  menu_was_down = pad_button(SDL_CONTROLLER_BUTTON_GUIDE);
  menu_spoiled = 1;

  LOG("closing the menu\n");
  if (result == MENU_QUIT) {
    request_quit();
  }
}

EXPORT void SDL_RenderPresent(void *renderer)
{
  static int presented;

  resolve_sdl();
  if (sdl.RenderPresent == NULL) {
    return;
  }

  init_display();
  if (!presented) {
    presented = 1;
    LOG("pico-8 presented its first frame\n");
  }
  if (quitting) {
    clear_to_black(renderer);
    sdl.RenderPresent(renderer);
    return;
  }

  update_buttons();
  if (sdl.GetTicks != NULL) {
    update_indicator(sdl.GetTicks(), menu_was_down);
  }
  draw_overlay(renderer);
  if (menu_requested) {
    menu_requested = 0;
    run_menu(renderer);
  } else {
    draw_indicator(renderer);
  }
  sdl.RenderPresent(renderer);
}

EXPORT int SDL_RenderCopy(void *renderer, void *texture, const SDL_Rect *src, const SDL_Rect *dst)
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

EXPORT int SDL_PollEvent(void *event)
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

EXPORT void *SDL_GameControllerOpen(int index)
{
  void *opened;

  resolve_sdl();
  if (sdl.GameControllerOpen == NULL) {
    return NULL;
  }

  opened = sdl.GameControllerOpen(index);
  if (opened != NULL) {
    controller = opened;
    LOG("watching controller %d for the Menu button\n", index);
  }

  return opened;
}

EXPORT void SDL_GameControllerClose(void *closing)
{
  resolve_sdl();

  if (closing == controller) {
    controller = NULL;
    menu_requested = 0;
    menu_was_down = 0;
    memset(hidden, 0, sizeof hidden);
    hidden_count = 0;
  }

  if (sdl.GameControllerClose != NULL) {
    sdl.GameControllerClose(closing);
  }
}

EXPORT uint8_t SDL_GameControllerGetButton(void *pad, int button)
{
  resolve_sdl();
  if (sdl.GameControllerGetButton == NULL) {
    return 0;
  }

  init_display();
  if (pad == controller && pad != NULL) {
    update_buttons();
    if (is_hidden(button)) {
      return 0;
    }
  }

  return sdl.GameControllerGetButton(pad, button);
}

EXPORT uint32_t SDL_OpenAudioDevice(const char *device, int iscapture, const void *desired,
                             void *obtained, int allowed_changes)
{
  uint32_t opened;

  resolve_sdl();
  if (sdl.OpenAudioDevice == NULL) {
    return 0;
  }

  opened = sdl.OpenAudioDevice(device, iscapture, desired, obtained, allowed_changes);
  if (opened != 0 && !iscapture) {
    audio_device = opened;
  }

  return opened;
}

/* the legacy call always opens device 1 */
EXPORT int SDL_OpenAudio(void *desired, void *obtained)
{
  int status;

  resolve_sdl();
  if (sdl.OpenAudio == NULL) {
    return -1;
  }

  status = sdl.OpenAudio(desired, obtained);
  if (status == 0) {
    audio_device = 1;
  }

  return status;
}

/* SDL frees a renderer's textures with it, so a later renderer needs its own. */
EXPORT void SDL_DestroyRenderer(void *renderer)
{
  resolve_sdl();

  if (renderer == overlay_renderer) {
    overlay_renderer = NULL;
    overlay_texture = NULL;
    loaded = -2;
  }
  if (renderer == indicator_renderer) {
    indicator_renderer = NULL;
    indicator_texture = NULL;
  }

  if (sdl.DestroyRenderer != NULL) {
    sdl.DestroyRenderer(renderer);
  }
}
