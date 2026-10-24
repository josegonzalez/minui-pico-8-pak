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
#define FAKE_AUDIO_DEVICE 2
/* the menu loop is stuck if it waits this many times */
#define FAKE_MAX_DELAYS 1000

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
  /* joystick buttons no mapping names, such as rg35xxplus's volume keys */
  uint8_t raw_buttons[20];

  /*
   * Each SDL_Delay sets the pad to the next mask, a bit per button, which is
   * how a test presses buttons while the menu holds pico-8 in SDL_RenderPresent.
   */
  uint32_t script[32];
  int script_length;
  int delays;

  int presents;
  int copies;
  struct fake_copy copy[256];

  int surfaces;
  int surface_w, surface_h, surface_depth, surface_pitch;
  uint32_t surface_masks[4];
  unsigned char surface_pixels[256];

  int textures_live;
  int streaming_textures;

  /* SDL_RenderReadPixels fills the frame with this, and records how it was asked */
  uint32_t frame_colour;
  int reads;
  uint32_t read_format;
  int read_pitch;
  struct fake_copy read_state;
  /* how many copies there had been when the frame was read */
  int copies_at_read;

  /* the first pixel of each SDL_UpdateTexture, and how many there were */
  int updates;
  uint32_t updated[256];
  int clears;

  /* SDL_GetTicks is this plus 16ms for every SDL_Delay there has ever been */
  uint32_t ticks;
  uint32_t all_delays;

  /* the draw colour, and how many clears were black */
  uint8_t draw_colour[4];
  int black_clears;

  /* libmsettings, which the shim opens from this same library in the tests */
  int settings_inits;
  int volume, brightness, mute;

  int audio_opened;
  int audio_status;
  int audio_pauses;
  int audio_resumes;

  int pushed;
  uint32_t pushed_type;
  /* every event pushed, as SDL_Event's 56 bytes */
  uint32_t pushed_events[16][14];

  /* what SDL_GetModState reports before the shim adds to it */
  int mod_state;

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
