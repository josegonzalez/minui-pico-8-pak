/*
 * A libSDL2-2.0.so.0 with only the functions sdl-display.c resolves, each
 * recording what it was asked into fake so display-test.c can check what the
 * shim did to the frame, the pad and the event queue.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fake-sdl.h"

struct fake_state fake = {
    .scale_x = 1.0f,
    .scale_y = 1.0f,
    .viewport = {0, 0, FAKE_OUTPUT_W, FAKE_OUTPUT_H},
};

struct fake_surface {
  int w, h;
};

static int pad;

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
  fake.pushed++;
  fake.pushed_type = *(uint32_t *)event;
  return 1;
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
  copy->logical_w = fake.logical_w;
  copy->logical_h = fake.logical_h;
  copy->scale_x = fake.scale_x;
  copy->scale_y = fake.scale_y;
  copy->viewport = fake.viewport;
  copy->clip_enabled = fake.clip_enabled;
  return 0;
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
