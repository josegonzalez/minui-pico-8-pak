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
 * Under NextUI it also gives the game switcher its preview of the cart: the
 * cart's artwork once pico-8 starts, and the game's last frame on Quit. The
 * menu works out its title from the cart the first time it opens, and gives
 * the Splore carts their cover as the setting changes. cart.c holds all three.
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
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "gfx.h"
#include "cart.h"
#include "crash.h"
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
#define SDL_KEYDOWN 0x300
#define SDL_KEYUP 0x301
#define SDL_SCANCODE_R 21
#define SDL_SCANCODE_LCTRL 224
#define SDLK_r 'r'
#define SDLK_LCTRL 0x400000e0
#define KMOD_LCTRL 0x0040
#define SDL_CONTROLLERAXISMOTION 0x650
#define SDL_CONTROLLERBUTTONDOWN 0x651
#define SDL_CONTROLLERBUTTONUP 0x652
#define SDL_CONTROLLER_AXIS_TRIGGERLEFT 4
#define SDL_CONTROLLER_AXIS_TRIGGERRIGHT 5
#define SDL_CONTROLLER_BUTTON_A 0
#define SDL_CONTROLLER_BUTTON_B 1
#define SDL_CONTROLLER_BUTTON_X 2
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
/* how long a notice such as "Screenshot saved" stays up */
#define NOTIFICATION_MS 1500
/* the steps the menu fades to black in after Quit, and how long each lasts */
#define FADE_STEPS 8
#define FADE_STEP_MS 20
/* how far a trigger, which SDL reads from 0 to 32767, goes in to count as pressed */
#define TRIGGER_DOWN 16384
/*
 * pico-8 ignores an R that comes too soon after Ctrl, so the reset shortcut
 * waits this long between pressing Ctrl, pressing R, and letting each go
 */
#define RESET_STEP_MS 100

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
  int16_t (*GameControllerGetAxis)(void *, int);
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
  int (*GetModState)(void);
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
/* buttons still held from the menu, hidden from pico-8 until released, numbered as menu.h does */
static uint8_t hidden[MENU_BINDABLE_BUTTONS];
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

/* the shortcut pressed, carried out by the next SDL_RenderPresent, or -1 */
static int pending_shortcut = -1;
/* where screenshots go, and whether one shows a notice */
static const char *screenshots_dir;
static int notify_screenshot = 1;
/* the screenshot being saved in the background, so a later one or exiting waits for it */
static pthread_t screenshot_thread;
static int screenshot_running;
/* the notice shown at the top right, and until when */
static const char *notification;
static uint32_t notification_until;
static void *notification_renderer;
static void *notification_texture;
static const char *notification_drawn;
/* Ctrl is reported held from the reset shortcut until pico-8 reads its release */
static int reset_ctrl_held;
/* how far the reset shortcut has got in pressing Ctrl+R, 0 when it is not, and since when */
static int reset_step;
static uint32_t reset_step_at;

static void wait_for_screenshot(void);

/* the cart pico-8 was started with, and the menu's title for it, worked out on first use */
static const char *cart_path;
static char cart_name[256];
/* NextUI's game switcher folder, unset under MinUI, which reads it for resuming */
static const char *switcher_dir;
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
  /* wrapped, and without it L2 and R2 only do nothing as shortcuts */
  RESOLVE(GameControllerGetAxis);
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
  /* wrapped, for the reset shortcut's Ctrl */
  RESOLVE(GetModState);

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

  /* first, so a crash anywhere after this says where it was */
  crash_report_install();

  settings.screen_mode_path = getenv("PICO_PAK_SCREEN_MODE");
  settings.screen_mode = menu_read_screen_mode(settings.screen_mode_path);
  LOG("starting in %s screen mode\n", menu_screen_modes[settings.screen_mode]);

  settings.overlay_path = getenv("PICO_PAK_OVERLAY_CHOICE");
  settings.copy_carts_dir = getenv("PICO_PAK_COPY_CARTS_DIR");
  settings.shared_copy_carts_dir = getenv("PICO_PAK_SHARED_COPY_CARTS_DIR");
  settings.splore_artwork_path = getenv("PICO_PAK_SPLORE_ARTWORK");
  settings.roms_dir = getenv("PICO_PAK_ROMS_DIR");
  settings.tag = getenv("PICO_PAK_TAG");
  settings.splore_covers_dir = getenv("PICO_PAK_SPLORE_COVERS");
  settings.pico8_config_path = getenv("PICO_PAK_PICO8_CONFIG");
  settings.installed_config_path = getenv("PICO_PAK_INSTALLED_CONFIG");
  settings.shortcuts_path = getenv("PICO_PAK_SHORTCUTS");
  menu_read_shortcuts(settings.shortcuts_path, settings.shortcuts);
  screenshots_dir = getenv("PICO_PAK_SCREENSHOTS_DIR");
  notify_screenshot = getenv("PICO_PAK_NOTIFY_SCREENSHOT") == NULL ||
                      strcmp(getenv("PICO_PAK_NOTIFY_SCREENSHOT"), "0") != 0;
  cart_path = getenv("PICO_PAK_CART_PATH");
  switcher_dir = getenv("PICO_PAK_SWITCHER_DIR");
  /* the artwork stands in until Quit gives the switcher a frame of the game */
  if (cart_path != NULL && *cart_path != '\0') {
    cart_write_preview_artwork(switcher_dir, cart_path);
  }
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

static int trigger_axis(int button)
{
  return button == MENU_BUTTON_L2 ? SDL_CONTROLLER_AXIS_TRIGGERLEFT
                                  : SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
}

/* the button a trigger's axis stands for, or -1 for any other axis */
static int trigger_button(int axis)
{
  return axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT    ? MENU_BUTTON_L2
         : axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT ? MENU_BUTTON_R2
                                                    : -1;
}

/* a button as menu.h numbers them, with L2 and R2 read from the triggers */
static uint8_t pad_button(int button)
{
  if (button == MENU_BUTTON_L2 || button == MENU_BUTTON_R2) {
    return sdl.GameControllerGetAxis != NULL &&
           sdl.GameControllerGetAxis(controller, trigger_axis(button)) >= TRIGGER_DOWN;
  }
  return sdl.GameControllerGetButton(controller, button);
}

/*
 * A Menu held with any other button is a chord for the OS, such as
 * brightness on rg35xxplus, whose volume keys are pad buttons no mapping
 * names, so the raw buttons are counted rather than the mapped ones. The
 * triggers are mostly axes rather than buttons, so they are counted too.
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
    return down > 1 || pad_button(MENU_BUTTON_L2) || pad_button(MENU_BUTTON_R2);
  }

  for (i = 0; i < MENU_BINDABLE_BUTTONS; i++) {
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
  static int shortcut_was_down[MENU_SHORTCUT_COUNT];
  int down, button, shortcut;

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

  /*
   * A shortcut's button does its action once a press. With Menu held only the
   * MENU+button ones count, and without it only the plain ones, so MENU+A and
   * A can do different things. The button stays from pico-8 until let go.
   */
  for (shortcut = 0; shortcut < MENU_SHORTCUT_COUNT; shortcut++) {
    const struct menu_binding *binding = &settings.shortcuts[shortcut];
    int pressed;

    if (!menu_is_bindable(binding->button)) {
      continue;
    }
    pressed = pad_button(binding->button);
    if (pressed && !shortcut_was_down[shortcut] && binding->with_menu == down &&
        pending_shortcut < 0) {
      pending_shortcut = shortcut;
      LOG("%s pressed\n", menu_shortcut_label(shortcut));
      if (!hidden[binding->button]) {
        hidden[binding->button] = 1;
        hidden_count++;
      }
    }
    shortcut_was_down[shortcut] = pressed;
  }

  for (button = 0; hidden_count > 0 && button < MENU_BINDABLE_BUTTONS; button++) {
    if (hidden[button] && !pad_button(button)) {
      hidden[button] = 0;
      hidden_count--;
    }
  }
}

/*
 * Menu never reaches pico-8, nor does a button bound to a shortcut: a plain
 * one ever, as in minarch, and a MENU+button one while Menu is held.
 */
static int is_hidden(int button)
{
  int shortcut;

  if (!menu_available() || button < 0 || button >= MENU_BINDABLE_BUTTONS) {
    return 0;
  }
  if (button == SDL_CONTROLLER_BUTTON_GUIDE || hidden[button]) {
    return 1;
  }

  for (shortcut = 0; shortcut < MENU_SHORTCUT_COUNT; shortcut++) {
    const struct menu_binding *binding = &settings.shortcuts[shortcut];
    if (binding->button == button && (!binding->with_menu || menu_was_down)) {
      return 1;
    }
  }
  return 0;
}

/*
 * Covers pico-8 reading the pad from events as well as from its state. An
 * axis event has its axis where a button event has its button.
 */
static int swallow_event(const void *event)
{
  const SDL_ControllerButtonEvent *button = event;
  int hidden_button;

  if (button->type == SDL_CONTROLLERAXISMOTION) {
    hidden_button = trigger_button(button->button);
  } else if (button->type == SDL_CONTROLLERBUTTONDOWN || button->type == SDL_CONTROLLERBUTTONUP) {
    hidden_button = button->button;
  } else {
    return 0;
  }

  update_buttons();
  return hidden_button >= 0 && is_hidden(hidden_button);
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
      {SDL_CONTROLLER_BUTTON_X, MENU_X},            {SDL_CONTROLLER_BUTTON_GUIDE, MENU_MENU},
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
static void fade_out(void *renderer, void *texture, gfx_canvas *canvas, int drawn, int start_dim)
{
  size_t count = (size_t)canvas->w * canvas->h;
  int step;

  /*
   * Fade from what is on screen: the menu if it drew, and otherwise the game,
   * dimmed by half when the menu was about to draw over it
   */
  if (drawn) {
    memcpy(menu_frame, canvas->pixels, count * sizeof *menu_frame);
  } else {
    gfx_dim(canvas, menu_frame, start_dim);
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
 * The Quit shortcut comes here too, with quit_now set, to quit as the menu's
 * Quit does without the menu ever drawing.
 */
static void run_menu(void *renderer, int quit_now)
{
  enum menu_result result = quit_now ? MENU_QUIT : MENU_STAY;
  struct letterbox saved;
  struct menu menu;
  gfx_canvas canvas;
  gfx_battery battery;
  uint32_t event[14];
  uint8_t raw_was_down[MENU_BINDABLE_BUTTONS];
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
  if (quit_now) {
    LOG("quitting from the shortcut%s\n", paused ? " and pausing pico-8's audio" : "");
  } else {
    LOG("opening the menu%s\n", paused ? " and pausing pico-8's audio" : "");
  }
  if (cart_name[0] == '\0' && cart_path != NULL && *cart_path != '\0') {
    cart_title(cart_path, cart_name, sizeof cart_name);
  }
  menu_open(&menu, &settings, cart_name);
  menu.indicator = indicator;
  menu.indicator_value = indicator_value;
  battery = gfx_read_battery(power_supply_dir);
  canvas.pixels = menu_pixels;
  canvas.w = width;
  canvas.h = height;

  /* a button already down when the menu opens is not a press */
  was_held = read_menu_buttons();
  for (button = 0; button < MENU_BINDABLE_BUTTONS; button++) {
    raw_was_down[button] = pad_button(button);
  }
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

    /*
     * A shortcut waiting for its button takes the next one pressed, with
     * Menu held or not, and only a tap of Menu alone reaches the menu, to
     * give up. A button already down when it started waiting does not count.
     */
    for (button = 0; button < MENU_BINDABLE_BUTTONS; button++) {
      int down = pad_button(button);

      if (menu_is_binding(&menu) && menu_is_bindable(button) && down && !raw_was_down[button]) {
        menu_bind(&menu, button, (held & MENU_MENU) != 0);
        menu_chord = 1;
        pressed = 0;
      }
      raw_was_down[button] = down;
    }
    if (menu_is_binding(&menu)) {
      pressed &= MENU_MENU;
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
    /*
     * The cart's sound stays paused and every frame left is black. The game
     * as the menu found it becomes the switcher's preview, written once the
     * screen is dark, as the fade draws over the copy it was read into.
     */
    uint32_t *last_frame = switcher_dir != NULL && *switcher_dir != '\0'
                               ? malloc((size_t)width * height * sizeof *last_frame)
                               : NULL;

    if (last_frame != NULL) {
      memcpy(last_frame, menu_frame, (size_t)width * height * sizeof *last_frame);
    }
    fade_out(renderer, texture, &canvas, drawn, quit_now ? 0 : 0x80);
    quitting = 1;
    LOG("faded out, every frame is black until pico-8 exits\n");
    if (last_frame != NULL && cart_path != NULL && *cart_path != '\0') {
      cart_write_preview_frame(switcher_dir, cart_path, last_frame, width, height);
    }
    free(last_frame);
  } else {
    show_pixels(renderer, texture, menu_frame, width);
    if (paused) {
      sdl.PauseAudioDevice(audio_device, 0);
    }
  }
  sdl.DestroyTexture(texture);
  restore_letterbox(renderer, &saved);

  /* the press that closed the menu is not the cart's */
  for (button = 0; button < MENU_BINDABLE_BUTTONS; button++) {
    if (!hidden[button] && pad_button(button)) {
      hidden[button] = 1;
      hidden_count++;
    }
  }
  menu_was_down = pad_button(SDL_CONTROLLER_BUTTON_GUIDE);
  menu_spoiled = 1;

  LOG("closing the menu\n");
  if (result == MENU_QUIT) {
    wait_for_screenshot();
    request_quit();
  }
}

struct screenshot {
  uint32_t *pixels;
  int w, h;
  time_t when;
};

static void *save_screenshot(void *data)
{
  struct screenshot *shot = data;

  cart_write_screenshot(screenshots_dir, cart_path, shot->pixels, shot->w, shot->h, shot->when,
                        NULL, 0);
  free(shot->pixels);
  free(shot);
  return NULL;
}

/* a screenshot still being saved is finished before another starts or pico-8 exits */
static void wait_for_screenshot(void)
{
  if (screenshot_running) {
    pthread_join(screenshot_thread, NULL);
    screenshot_running = 0;
  }
}

/*
 * Takes the screen as it is shown, overlay and all, and saves it on a thread
 * of its own, as encoding a PNG on these cores would hold up the game.
 */
static void take_screenshot(void *renderer)
{
  static int waits_at_exit;
  struct screenshot *shot;
  struct letterbox saved;
  size_t i, count;
  int width, height;

  if (screenshots_dir == NULL || *screenshots_dir == '\0' || cart_path == NULL ||
      sdl.GetRendererOutputSize(renderer, &width, &height) != 0) {
    LOG("cannot take a screenshot without a folder and a cart\n");
    return;
  }

  count = (size_t)width * height;
  shot = malloc(sizeof *shot);
  if (shot == NULL || (shot->pixels = malloc(count * sizeof *shot->pixels)) == NULL) {
    free(shot);
    return;
  }

  clear_letterbox(renderer, &saved);
  if (sdl.RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, shot->pixels,
                           width * (int)sizeof *shot->pixels) != 0) {
    restore_letterbox(renderer, &saved);
    LOG("cannot read the screen for a screenshot: %s\n", sdl.GetError());
    free(shot->pixels);
    free(shot);
    return;
  }
  restore_letterbox(renderer, &saved);
  for (i = 0; i < count; i++) {
    shot->pixels[i] |= 0xff000000u;
  }
  shot->w = width;
  shot->h = height;
  shot->when = time(NULL);

  wait_for_screenshot();
  if (!waits_at_exit) {
    atexit(wait_for_screenshot);
    waits_at_exit = 1;
  }
  if (pthread_create(&screenshot_thread, NULL, save_screenshot, shot) != 0) {
    save_screenshot(shot);
  } else {
    screenshot_running = 1;
  }

  if (notify_screenshot && sdl.GetTicks != NULL) {
    notification = "Screenshot saved";
    notification_until = sdl.GetTicks() + NOTIFICATION_MS;
  }
}

/* the notice at the top right, below the indicator when that is showing too */
static void draw_notification(void *renderer)
{
  static int texture_w, texture_h;
  struct letterbox saved;
  gfx_canvas canvas;
  gfx_metrics m;
  SDL_Rect dst;
  int width, height, w, h, indicator_w, indicator_h;
  uint32_t *pixels;

  if (notification == NULL || sdl.GetTicks == NULL ||
      (int32_t)(sdl.GetTicks() - notification_until) >= 0) {
    notification = NULL;
    return;
  }
  if (overlay_broken || menu_broken || sdl.GetRendererOutputSize(renderer, &width, &height) != 0) {
    return;
  }

  load_menu_look();
  menu_notification_size(width, height, menu_font, notification, &w, &h);
  if (renderer != notification_renderer || w != texture_w || h != texture_h ||
      notification != notification_drawn) {
    if (notification_texture != NULL && renderer == notification_renderer) {
      sdl.DestroyTexture(notification_texture);
    }
    notification_renderer = renderer;
    notification_drawn = NULL;
    notification_texture = sdl.CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                             SDL_TEXTUREACCESS_STREAMING, w, h);
    pixels = calloc((size_t)w * h, sizeof *pixels);
    if (notification_texture == NULL || pixels == NULL) {
      free(pixels);
      return;
    }
    canvas.pixels = pixels;
    canvas.w = w;
    canvas.h = h;
    menu_draw_notification(&canvas, width, height, &menu_theme, menu_font, 0, 0, notification);
    sdl.UpdateTexture(notification_texture, NULL, pixels, w * (int)sizeof *pixels);
    sdl.SetTextureBlendMode(notification_texture, SDL_BLENDMODE_BLEND);
    free(pixels);
    texture_w = w;
    texture_h = h;
    notification_drawn = notification;
  }

  m = gfx_metrics_for(width, height);
  dst.x = width - gfx_u(&m, m.padding) - w;
  dst.y = gfx_u(&m, m.padding);
  if (indicator != MENU_INDICATOR_NONE) {
    menu_indicator_size(width, height, &indicator_w, &indicator_h);
    dst.y += indicator_h + gfx_u(&m, m.padding);
  }
  dst.w = w;
  dst.h = h;
  clear_letterbox(renderer, &saved);
  sdl.RenderCopy(renderer, notification_texture, NULL, &dst);
  restore_letterbox(renderer, &saved);
}

static void push_key(uint32_t type, int32_t scancode, int32_t sym, uint16_t mod)
{
  /* SDL_KeyboardEvent: the state at byte 12, then the scancode, symbol and modifiers */
  uint32_t event[14] = {type};
  unsigned char *bytes = (unsigned char *)event;

  bytes[12] = type == SDL_KEYDOWN ? 1 : 0;
  memcpy(bytes + 16, &scancode, sizeof scancode);
  memcpy(bytes + 20, &sym, sizeof sym);
  memcpy(bytes + 24, &mod, sizeof mod);
  sdl.PushEvent(event);
}

/*
 * pico-8 restarts the cart on Ctrl+R, so the shortcut presses it: Ctrl down,
 * R down and up, then Ctrl up, a step every RESET_STEP_MS as the frames go
 * by. Until pico-8 reads that last release, Ctrl is also reported held to
 * SDL_GetModState, which is where pico-8 reads it from.
 */
static void reset_cart(void)
{
  if (sdl.PushEvent == NULL) {
    LOG("this SDL2 has no SDL_PushEvent, cannot reset the cart\n");
    return;
  }
  if (reset_step != 0) {
    return;
  }

  LOG("resetting the cart with Ctrl+R\n");
  reset_ctrl_held = 1;
  reset_step = 1;
  reset_step_at = sdl.GetTicks();
  push_key(SDL_KEYDOWN, SDL_SCANCODE_LCTRL, SDLK_LCTRL, KMOD_LCTRL);
}

static void continue_reset(void)
{
  if (reset_step == 0 || sdl.GetTicks() - reset_step_at < RESET_STEP_MS) {
    return;
  }

  reset_step_at = sdl.GetTicks();
  switch (reset_step++) {
  case 1:
    push_key(SDL_KEYDOWN, SDL_SCANCODE_R, SDLK_r, KMOD_LCTRL);
    break;
  case 2:
    push_key(SDL_KEYUP, SDL_SCANCODE_R, SDLK_r, KMOD_LCTRL);
    break;
  default:
    push_key(SDL_KEYUP, SDL_SCANCODE_LCTRL, SDLK_LCTRL, 0);
    reset_step = 0;
    break;
  }
}

/* the shortcuts that change things before the frame is drawn */
static void run_shortcut(int shortcut)
{
  switch (shortcut) {
  case MENU_SHORTCUT_RESET:
    reset_cart();
    break;
  case MENU_SHORTCUT_SCREEN_MODE:
    menu_cycle_screen_mode(&settings);
    break;
  case MENU_SHORTCUT_OVERLAY:
    menu_cycle_overlay(&settings);
    break;
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
    continue_reset();
  }
  /*
   * The menu reads the frame before the overlay is drawn over it, so the
   * switcher's preview after Quit is the game alone. The frame it puts back
   * then gets the overlay as any other frame would.
   */
  if (menu_requested || pending_shortcut == MENU_SHORTCUT_QUIT) {
    int quit_now = !menu_requested;

    menu_requested = 0;
    pending_shortcut = -1;
    run_menu(renderer, quit_now);
    if (quitting) {
      clear_to_black(renderer);
      sdl.RenderPresent(renderer);
      return;
    }
  }
  if (pending_shortcut >= 0 && pending_shortcut != MENU_SHORTCUT_SCREENSHOT) {
    run_shortcut(pending_shortcut);
    pending_shortcut = -1;
  }

  draw_overlay(renderer);
  /* the screen as it is shown, without the notices drawn over it */
  if (pending_shortcut == MENU_SHORTCUT_SCREENSHOT) {
    pending_shortcut = -1;
    take_screenshot(renderer);
  }
  draw_indicator(renderer);
  draw_notification(renderer);
  sdl.RenderPresent(renderer);
}

EXPORT int SDL_GetModState(void)
{
  resolve_sdl();
  return (sdl.GetModState != NULL ? sdl.GetModState() : 0) | (reset_ctrl_held ? KMOD_LCTRL : 0);
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

  /* the reset shortcut's Ctrl is let go once pico-8 has read it being let go */
  if (got && reset_ctrl_held && *(const uint32_t *)event == SDL_KEYUP &&
      *(const int32_t *)((const unsigned char *)event + 16) == SDL_SCANCODE_LCTRL) {
    reset_ctrl_held = 0;
  }

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

/* a trigger bound to a shortcut is hidden from pico-8 as a button would be */
EXPORT int16_t SDL_GameControllerGetAxis(void *pad, int axis)
{
  resolve_sdl();
  if (sdl.GameControllerGetAxis == NULL) {
    return 0;
  }

  init_display();
  if (pad == controller && pad != NULL) {
    update_buttons();
    if (trigger_button(axis) >= 0 && is_hidden(trigger_button(axis))) {
      return 0;
    }
  }

  return sdl.GameControllerGetAxis(pad, axis);
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
  if (renderer == notification_renderer) {
    notification_renderer = NULL;
    notification_texture = NULL;
  }

  if (sdl.DestroyRenderer != NULL) {
    sdl.DestroyRenderer(renderer);
  }
}
