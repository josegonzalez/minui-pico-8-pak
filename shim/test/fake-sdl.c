/*
 * A libSDL2-2.0.so.0 with only the functions sdl-display.c resolves, each
 * recording what it was asked into fake so display-test.c can check what the
 * shim did to the frame, the pad, the audio and the event queue. It also
 * stands in for the launcher's libmsettings, which the shim opens by path.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake-sdl.h"

#define SDL_AUDIO_PLAYING 1

struct fake_state fake = {
    .scale_x = 1.0f,
    .scale_y = 1.0f,
    .viewport = {0, 0, FAKE_OUTPUT_W, FAKE_OUTPUT_H},
    .audio_status = SDL_AUDIO_PLAYING,
};

struct fake_surface {
  int w, h;
};

static int pad;
static int joystick;

/* the renderer state as a copy made now would see it */
static void record_state(struct fake_copy *copy)
{
  copy->logical_w = fake.logical_w;
  copy->logical_h = fake.logical_h;
  copy->scale_x = fake.scale_x;
  copy->scale_y = fake.scale_y;
  copy->viewport = fake.viewport;
  copy->clip_enabled = fake.clip_enabled;
}

void SDL_RenderPresent(void *renderer)
{
  (void)renderer;
  fake.presents++;
}

void SDL_DestroyRenderer(void *renderer)
{
  (void)renderer;
  /* SDL frees a renderer's textures with it */
  fake.textures_live = 0;
}

int SDL_PollEvent(void *event)
{
  if (fake.queue_head >= fake.queued) {
    return 0;
  }
  if (event != NULL) {
    memcpy(event, fake.queue[fake.queue_head++], sizeof fake.queue[0]);
  }
  return 1;
}

int SDL_PushEvent(void *event)
{
  if (fake.pushed < (int)(sizeof fake.pushed_events / sizeof *fake.pushed_events)) {
    memcpy(fake.pushed_events[fake.pushed], event, sizeof fake.pushed_events[0]);
  }
  fake.pushed++;
  fake.pushed_type = *(uint32_t *)event;
  return 1;
}

int SDL_GetModState(void)
{
  return fake.mod_state;
}

void *SDL_GameControllerOpen(int index)
{
  (void)index;
  return &pad;
}

void SDL_GameControllerClose(void *controller)
{
  (void)controller;
}

uint8_t SDL_GameControllerGetButton(void *controller, int button)
{
  if (controller != &pad || button < 0 || button >= (int)sizeof fake.buttons) {
    return 0;
  }
  return fake.buttons[button];
}

int16_t SDL_GameControllerGetAxis(void *controller, int axis)
{
  if (controller != &pad || axis < 0 || axis >= (int)(sizeof fake.axes / sizeof *fake.axes)) {
    return 0;
  }
  return fake.axes[axis];
}

void *SDL_CreateRGBSurfaceFrom(void *pixels, int width, int height, int depth, int pitch,
                               uint32_t rmask, uint32_t gmask, uint32_t bmask, uint32_t amask)
{
  struct fake_surface *surface = malloc(sizeof *surface);
  size_t size = (size_t)pitch * height;

  if (surface == NULL) {
    return NULL;
  }
  surface->w = width;
  surface->h = height;

  fake.surfaces++;
  fake.surface_w = width;
  fake.surface_h = height;
  fake.surface_depth = depth;
  fake.surface_pitch = pitch;
  fake.surface_masks[0] = rmask;
  fake.surface_masks[1] = gmask;
  fake.surface_masks[2] = bmask;
  fake.surface_masks[3] = amask;
  memcpy(fake.surface_pixels, pixels,
         size < sizeof fake.surface_pixels ? size : sizeof fake.surface_pixels);
  return surface;
}

void SDL_FreeSurface(void *surface)
{
  free(surface);
}

void *SDL_CreateTextureFromSurface(void *renderer, void *surface)
{
  struct fake_surface *from = surface;
  struct fake_texture *texture = malloc(sizeof *texture);

  (void)renderer;
  if (texture == NULL) {
    return NULL;
  }
  texture->w = from->w;
  texture->h = from->h;
  fake.textures_live++;
  return texture;
}

void SDL_DestroyTexture(void *texture)
{
  free(texture);
  fake.textures_live--;
}

int SDL_SetTextureBlendMode(void *texture, int mode)
{
  (void)texture;
  (void)mode;
  return 0;
}

int SDL_SetTextureScaleMode(void *texture, int mode)
{
  (void)texture;
  (void)mode;
  return 0;
}

const char *SDL_GetError(void)
{
  return "fake SDL error";
}

int SDL_RenderCopy(void *renderer, void *texture, const fake_rect *src, const fake_rect *dst)
{
  struct fake_copy *copy;

  (void)renderer;
  (void)src;
  if (fake.copies >= (int)(sizeof fake.copy / sizeof *fake.copy)) {
    return -1;
  }

  copy = &fake.copy[fake.copies++];
  copy->texture = texture;
  copy->has_dst = dst != NULL;
  if (dst != NULL) {
    copy->dst = *dst;
  }
  record_state(copy);
  return 0;
}

int SDL_RenderClear(void *renderer)
{
  (void)renderer;
  fake.clears++;
  if (fake.draw_colour[0] == 0 && fake.draw_colour[1] == 0 && fake.draw_colour[2] == 0) {
    fake.black_clears++;
  }
  return 0;
}

int SDL_RenderGetDrawColor(void *renderer, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a)
{
  (void)renderer;
  *r = fake.draw_colour[0];
  *g = fake.draw_colour[1];
  *b = fake.draw_colour[2];
  *a = fake.draw_colour[3];
  return 0;
}

int SDL_RenderSetDrawColor(void *renderer, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
  (void)renderer;
  fake.draw_colour[0] = r;
  fake.draw_colour[1] = g;
  fake.draw_colour[2] = b;
  fake.draw_colour[3] = a;
  return 0;
}

/* libmsettings' own names, as the shim looks them up with dlsym */
void InitSettings(void)
{
  fake.settings_inits++;
}

int GetVolume(void)
{
  return fake.volume;
}

int GetBrightness(void)
{
  return fake.brightness;
}

int GetMute(void)
{
  return fake.mute;
}

int SDL_RenderReadPixels(void *renderer, const fake_rect *rect, uint32_t format, void *pixels,
                         int pitch)
{
  uint32_t *out = pixels;
  int i;

  (void)renderer;
  (void)rect;
  fake.reads++;
  fake.read_format = format;
  fake.read_pitch = pitch;
  record_state(&fake.read_state);
  fake.copies_at_read = fake.copies;
  for (i = 0; i < FAKE_OUTPUT_W * FAKE_OUTPUT_H; i++) {
    out[i] = fake.frame_colour;
  }
  return 0;
}

void *SDL_CreateTexture(void *renderer, uint32_t format, int access, int w, int h)
{
  struct fake_texture *texture = malloc(sizeof *texture);

  (void)renderer;
  (void)format;
  (void)access;
  if (texture == NULL) {
    return NULL;
  }
  texture->w = w;
  texture->h = h;
  fake.textures_live++;
  fake.streaming_textures++;
  return texture;
}

int SDL_UpdateTexture(void *texture, const fake_rect *rect, const void *pixels, int pitch)
{
  (void)texture;
  (void)rect;
  (void)pitch;
  if (fake.updates < (int)(sizeof fake.updated / sizeof *fake.updated)) {
    fake.updated[fake.updates] = *(const uint32_t *)pixels;
  }
  fake.updates++;
  return 0;
}

void SDL_Delay(uint32_t ms)
{
  int button;

  (void)ms;
  if (fake.delays < fake.script_length) {
    for (button = 0; button < 32; button++) {
      fake.buttons[button] = (fake.script[fake.delays] >> button) & 1;
    }
  }
  fake.all_delays++;
  if (++fake.delays > FAKE_MAX_DELAYS) {
    printf("Bail out! the menu never closed\n");
    exit(3);
  }
}

uint32_t SDL_GetTicks(void)
{
  return fake.ticks + fake.all_delays * 16;
}

uint32_t SDL_OpenAudioDevice(const char *device, int iscapture, const void *desired,
                             void *obtained, int allowed_changes)
{
  (void)device;
  (void)iscapture;
  (void)desired;
  (void)obtained;
  (void)allowed_changes;
  fake.audio_opened++;
  return FAKE_AUDIO_DEVICE;
}

int SDL_OpenAudio(void *desired, void *obtained)
{
  (void)desired;
  (void)obtained;
  fake.audio_opened++;
  return 0;
}

int SDL_GetAudioDeviceStatus(uint32_t device)
{
  return device == FAKE_AUDIO_DEVICE ? fake.audio_status : 0;
}

void SDL_PauseAudioDevice(uint32_t device, int pause_on)
{
  if (device != FAKE_AUDIO_DEVICE) {
    return;
  }
  if (pause_on) {
    fake.audio_pauses++;
  } else {
    fake.audio_resumes++;
  }
}

void *SDL_GameControllerGetJoystick(void *controller)
{
  return controller == &pad ? &joystick : NULL;
}

int SDL_JoystickNumButtons(void *stick)
{
  return stick == &joystick ? (int)sizeof fake.raw_buttons : 0;
}

/* every mapped button is a joystick button too, numbered alike for simplicity */
uint8_t SDL_JoystickGetButton(void *stick, int button)
{
  if (stick != &joystick || button < 0 || button >= (int)sizeof fake.raw_buttons) {
    return 0;
  }
  return fake.raw_buttons[button] || fake.buttons[button];
}

void SDL_RenderGetLogicalSize(void *renderer, int *w, int *h)
{
  (void)renderer;
  *w = fake.logical_w;
  *h = fake.logical_h;
}

int SDL_RenderSetLogicalSize(void *renderer, int w, int h)
{
  (void)renderer;
  fake.logical_w = w;
  fake.logical_h = h;
  return 0;
}

void SDL_RenderGetViewport(void *renderer, fake_rect *rect)
{
  (void)renderer;
  *rect = fake.viewport;
}

int SDL_RenderSetViewport(void *renderer, const fake_rect *rect)
{
  static const fake_rect whole = {0, 0, FAKE_OUTPUT_W, FAKE_OUTPUT_H};

  (void)renderer;
  fake.viewport = rect != NULL ? *rect : whole;
  return 0;
}

void SDL_RenderGetScale(void *renderer, float *x, float *y)
{
  (void)renderer;
  *x = fake.scale_x;
  *y = fake.scale_y;
}

int SDL_RenderSetScale(void *renderer, float x, float y)
{
  (void)renderer;
  fake.scale_x = x;
  fake.scale_y = y;
  return 0;
}

int SDL_RenderIsClipEnabled(void *renderer)
{
  (void)renderer;
  return fake.clip_enabled;
}

void SDL_RenderGetClipRect(void *renderer, fake_rect *rect)
{
  static const fake_rect empty = {0, 0, 0, 0};

  (void)renderer;
  *rect = fake.clip_enabled ? fake.clip : empty;
}

int SDL_RenderSetClipRect(void *renderer, const fake_rect *rect)
{
  (void)renderer;
  fake.clip_enabled = rect != NULL;
  if (rect != NULL) {
    fake.clip = *rect;
  }
  return 0;
}

int SDL_GetRendererOutputSize(void *renderer, int *w, int *h)
{
  (void)renderer;
  *w = FAKE_OUTPUT_W;
  *h = FAKE_OUTPUT_H;
  return 0;
}

int SDL_QueryTexture(void *texture, uint32_t *format, int *access, int *w, int *h)
{
  struct fake_texture *query = texture;

  if (format != NULL) {
    *format = 0;
  }
  if (access != NULL) {
    *access = 0;
  }
  if (w != NULL) {
    *w = query->w;
  }
  if (h != NULL) {
    *h = query->h;
  }
  return 0;
}
