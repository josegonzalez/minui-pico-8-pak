/*
 * What the fake libSDL2 records, for display-test.c to assert on. The fake
 * stands in for the device's SDL2 behind sdl-display.so, which reaches it with
 * dlsym(RTLD_NEXT) exactly as it reaches the real one.
 */

#ifndef FAKE_SDL_H
#define FAKE_SDL_H

#include <stdint.h>

#define FAKE_OUTPUT_W 1024
#define FAKE_OUTPUT_H 768

typedef struct {
  int x, y, w, h;
} fake_rect;

struct fake_texture {
  int w, h;
};

/* the renderer state at the time of one SDL_RenderCopy */
struct fake_copy {
  void *texture;
  int has_dst;
  fake_rect dst;
  int logical_w, logical_h;
  float scale_x, scale_y;
  fake_rect viewport;
  int clip_enabled;
};

struct fake_state {
  /* indexed by SDL_GameControllerButton */
  uint8_t buttons[32];

  int presents;
  int copies;
  struct fake_copy copy[64];

  int surfaces;
  int surface_w, surface_h, surface_depth, surface_pitch;
  uint32_t surface_masks[4];
  unsigned char surface_pixels[256];

  int textures_live;

  int pushed;
  uint32_t pushed_type;

  /* SDL_Event is a 56 byte union */
  uint32_t queue[8][14];
  int queued, queue_head;

  int logical_w, logical_h;
  float scale_x, scale_y;
  fake_rect viewport;
  int clip_enabled;
  fake_rect clip;
};

extern struct fake_state fake;

#endif
