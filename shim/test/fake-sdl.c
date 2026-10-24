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

static struct fake_gl_call *log_call(const char *name, int a, int b, int c, int d)
{
  struct fake_gl_call *call;

  if (fake.gl_call_count >= (int)(sizeof fake.gl_calls / sizeof *fake.gl_calls)) {
    return NULL;
  }
  call = &fake.gl_calls[fake.gl_call_count++];
  memset(call, 0, sizeof *call);
  snprintf(call->name, sizeof call->name, "%s", name);
  call->args[0] = a;
  call->args[1] = b;
  call->args[2] = c;
  call->args[3] = d;
  return call;
}

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
  texture->format = 0;
  texture->access = 0;
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

int SDL_SetTextureAlphaMod(void *texture, uint8_t alpha)
{
  (void)texture;
  fake.alpha_mods++;
  fake.last_alpha = alpha;
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
  if (texture == NULL) {
    return NULL;
  }
  texture->w = w;
  texture->h = h;
  texture->format = format;
  texture->access = access;
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

void SDL_LockAudioDevice(uint32_t device)
{
  if (device == FAKE_AUDIO_DEVICE) {
    fake.audio_locks++;
  }
}

void SDL_UnlockAudioDevice(uint32_t device)
{
  if (device == FAKE_AUDIO_DEVICE) {
    fake.audio_unlocks++;
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
    *format = query->format;
  }
  if (access != NULL) {
    *access = query->access;
  }
  if (w != NULL) {
    *w = query->w;
  }
  if (h != NULL) {
    *h = query->h;
  }
  return 0;
}

/* SDL's GLES renderer, and the GL behind it */
typedef struct {
  const char *name;
  uint32_t flags;
  uint32_t num_texture_formats;
  uint32_t texture_formats[16];
  int max_texture_width;
  int max_texture_height;
} fake_renderer_info;

int SDL_GetRendererInfo(void *renderer, fake_renderer_info *info)
{
  (void)renderer;
  memset(info, 0, sizeof *info);
  info->name = fake.renderer_name != NULL ? fake.renderer_name : "opengles2";
  return 0;
}

int SDL_RenderFlush(void *renderer)
{
  (void)renderer;
  fake.flushes++;
  log_call("SDL_RenderFlush", 0, 0, 0, 0);
  return 0;
}

static int context;

void *SDL_GL_GetCurrentContext(void)
{
  return fake.no_gl_context ? NULL : &context;
}

#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_VIEWPORT 0x0BA2
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_VERSION 0x1F02
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_TEXTURE0 0x84C0
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_VERTEX_ATTRIB_ARRAY_ENABLED 0x8622
#define GL_ARRAY_BUFFER_BINDING 0x8894
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_CURRENT_PROGRAM 0x8B8D
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5

static unsigned next_name = 100;
/* the shaders whose source had FAIL in it */
static unsigned failed_shaders[64];
static int failed_count;

static const unsigned char *fake_glGetString(unsigned name)
{
  return (const unsigned char *)(name == GL_VERSION && fake.gl_version != NULL
                                     ? fake.gl_version
                                     : name == GL_VERSION ? "OpenGL ES 3.2 fake" : "fake");
}

static void fake_glGetIntegerv(unsigned name, int *value)
{
  int unit = fake.gl.active_texture == GL_TEXTURE0 + 1;

  switch (name) {
  case GL_MAX_TEXTURE_SIZE:
    *value = 4096;
    break;
  case GL_CURRENT_PROGRAM:
    *value = fake.gl.program;
    break;
  case GL_ACTIVE_TEXTURE:
    *value = fake.gl.active_texture;
    break;
  case GL_TEXTURE_BINDING_2D:
    *value = fake.gl.textures[unit];
    break;
  case GL_ARRAY_BUFFER_BINDING:
    *value = fake.gl.array_buffer;
    break;
  case GL_FRAMEBUFFER_BINDING:
    *value = fake.gl.framebuffer;
    break;
  case GL_VIEWPORT:
    memcpy(value, fake.gl.viewport, sizeof fake.gl.viewport);
    break;
  default:
    *value = 0;
    break;
  }
}

static void fake_glGetVertexAttribiv(unsigned index, unsigned name, int *value)
{
  *value = name == GL_VERTEX_ATTRIB_ARRAY_ENABLED && index < 8 ? fake.gl.attribs[index] : 0;
}

static unsigned char fake_glIsEnabled(unsigned cap)
{
  return cap == GL_BLEND ? fake.gl.blend : cap == GL_SCISSOR_TEST ? fake.gl.scissor : 0;
}

static void fake_glEnable(unsigned cap)
{
  log_call("glEnable", (int)cap, 0, 0, 0);
  if (cap == GL_BLEND) {
    fake.gl.blend = 1;
  } else if (cap == GL_SCISSOR_TEST) {
    fake.gl.scissor = 1;
  }
}

static void fake_glDisable(unsigned cap)
{
  log_call("glDisable", (int)cap, 0, 0, 0);
  if (cap == GL_BLEND) {
    fake.gl.blend = 0;
  } else if (cap == GL_SCISSOR_TEST) {
    fake.gl.scissor = 0;
  }
}

static unsigned fake_glCreateShader(unsigned type)
{
  log_call("glCreateShader", (int)type, 0, 0, 0);
  return next_name++;
}

static void fake_glShaderSource(unsigned shader, int count, const char *const *sources,
                                const int *lengths)
{
  (void)count;
  (void)lengths;
  if (fake.gl_source_count < (int)(sizeof fake.gl_sources / sizeof *fake.gl_sources)) {
    snprintf(fake.gl_sources[fake.gl_source_count++], sizeof fake.gl_sources[0], "%s", sources[0]);
  }
  if (strstr(sources[0], "FAIL") != NULL && failed_count < 64) {
    failed_shaders[failed_count++] = shader;
  }
}

static void fake_glCompileShader(unsigned shader)
{
  log_call("glCompileShader", (int)shader, 0, 0, 0);
}

static void fake_glGetShaderiv(unsigned shader, unsigned name, int *value)
{
  int i;

  *value = 1;
  for (i = 0; name == GL_COMPILE_STATUS && i < failed_count; i++) {
    if (failed_shaders[i] == shader) {
      *value = 0;
    }
  }
}

static void fake_glGetShaderInfoLog(unsigned shader, int size, int *length, char *log)
{
  (void)shader;
  (void)length;
  snprintf(log, size, "fake compile error");
}

static void fake_glDeleteShader(unsigned shader)
{
  (void)shader;
}

static unsigned fake_glCreateProgram(void)
{
  return next_name++;
}

static void fake_glAttachShader(unsigned program, unsigned shader)
{
  (void)program;
  (void)shader;
}

static void fake_glBindAttribLocation(unsigned program, unsigned index, const char *name)
{
  (void)program;
  (void)index;
  (void)name;
}

static void fake_glLinkProgram(unsigned program)
{
  log_call("glLinkProgram", (int)program, 0, 0, 0);
}

static void fake_glGetProgramiv(unsigned program, unsigned name, int *value)
{
  (void)program;
  *value = name == GL_LINK_STATUS;
}

static void fake_glGetProgramInfoLog(unsigned program, int size, int *length, char *log)
{
  (void)program;
  (void)length;
  snprintf(log, size, "fake link error");
}

static void fake_glDeleteProgram(unsigned program)
{
  (void)program;
}

static void fake_glUseProgram(unsigned program)
{
  log_call("glUseProgram", (int)program, 0, 0, 0);
  fake.gl.program = (int)program;
}

static int fake_glGetUniformLocation(unsigned program, const char *name)
{
  int i;

  (void)program;
  for (i = 0; i < fake.uniform_count; i++) {
    if (strcmp(fake.uniforms[i], name) == 0) {
      return i;
    }
  }
  if (fake.uniform_count >= 64) {
    return -1;
  }
  snprintf(fake.uniforms[fake.uniform_count], sizeof fake.uniforms[0], "%s", name);
  return fake.uniform_count++;
}

static void fake_glUniform1i(int location, int value)
{
  log_call("glUniform1i", location, value, 0, 0);
}

static void fake_glUniform1f(int location, float value)
{
  struct fake_gl_call *call = log_call("glUniform1f", location, 0, 0, 0);

  if (call != NULL) {
    call->floats[0] = value;
  }
}

static void fake_glUniform2f(int location, float x, float y)
{
  struct fake_gl_call *call = log_call("glUniform2f", location, 0, 0, 0);

  if (call != NULL) {
    call->floats[0] = x;
    call->floats[1] = y;
  }
}

static void fake_glUniformMatrix4fv(int location, int count, unsigned char transpose,
                                    const float *value)
{
  (void)count;
  (void)transpose;
  (void)value;
  log_call("glUniformMatrix4fv", location, 0, 0, 0);
}

static void fake_glGenTextures(int count, unsigned *names)
{
  int i;

  for (i = 0; i < count; i++) {
    names[i] = next_name++;
  }
}

static void fake_glDeleteTextures(int count, const unsigned *names)
{
  (void)count;
  (void)names;
}

static void fake_glBindTexture(unsigned target, unsigned texture)
{
  (void)target;
  log_call("glBindTexture", (int)texture, fake.gl.active_texture - GL_TEXTURE0, 0, 0);
  fake.gl.textures[fake.gl.active_texture == GL_TEXTURE0 + 1] = (int)texture;
}

static void fake_glActiveTexture(unsigned unit)
{
  fake.gl.active_texture = (int)unit;
}

static void fake_glTexParameteri(unsigned target, unsigned name, int value)
{
  (void)target;
  log_call("glTexParameteri", fake.gl.textures[fake.gl.active_texture == GL_TEXTURE0 + 1],
           (int)name, value, 0);
}

static void log_upload(const char *name, int w, int h, const void *pixels)
{
  struct fake_gl_call *call = log_call(name, fake.gl.textures[fake.gl.active_texture == GL_TEXTURE0 + 1],
                                       w, h, pixels != NULL);

  if (call != NULL && pixels != NULL) {
    memcpy(call->pixel, pixels, 4);
  }
}

static void fake_glTexImage2D(unsigned target, int level, int internal, int w, int h, int border,
                              unsigned format, unsigned type, const void *pixels)
{
  (void)target;
  (void)level;
  (void)internal;
  (void)border;
  (void)format;
  (void)type;
  log_upload("glTexImage2D", w, h, pixels);
}

static void fake_glTexSubImage2D(unsigned target, int level, int x, int y, int w, int h,
                                 unsigned format, unsigned type, const void *pixels)
{
  (void)target;
  (void)level;
  (void)x;
  (void)y;
  (void)format;
  (void)type;
  log_upload("glTexSubImage2D", w, h, pixels);
}

static void fake_glGenFramebuffers(int count, unsigned *names)
{
  fake_glGenTextures(count, names);
}

static void fake_glDeleteFramebuffers(int count, const unsigned *names)
{
  (void)count;
  (void)names;
}

static void fake_glBindFramebuffer(unsigned target, unsigned framebuffer)
{
  (void)target;
  log_call("glBindFramebuffer", (int)framebuffer, 0, 0, 0);
  fake.gl.framebuffer = (int)framebuffer;
}

static void fake_glFramebufferTexture2D(unsigned target, unsigned attachment, unsigned textarget,
                                        unsigned texture, int level)
{
  (void)target;
  (void)attachment;
  (void)textarget;
  (void)level;
  log_call("glFramebufferTexture2D", (int)texture, 0, 0, 0);
}

static unsigned fake_glCheckFramebufferStatus(unsigned target)
{
  (void)target;
  return GL_FRAMEBUFFER_COMPLETE;
}

static void fake_glViewport(int x, int y, int w, int h)
{
  log_call("glViewport", x, y, w, h);
  fake.gl.viewport[0] = x;
  fake.gl.viewport[1] = y;
  fake.gl.viewport[2] = w;
  fake.gl.viewport[3] = h;
}

static void fake_glBindBuffer(unsigned target, unsigned buffer)
{
  (void)target;
  fake.gl.array_buffer = (int)buffer;
}

static void fake_glVertexAttribPointer(unsigned index, int size, unsigned type,
                                       unsigned char normalized, int stride, const void *pointer)
{
  (void)index;
  (void)size;
  (void)type;
  (void)normalized;
  (void)stride;
  (void)pointer;
}

static void fake_glEnableVertexAttribArray(unsigned index)
{
  if (index < 8) {
    fake.gl.attribs[index] = 1;
  }
}

static void fake_glDisableVertexAttribArray(unsigned index)
{
  if (index < 8) {
    fake.gl.attribs[index] = 0;
  }
}

static void fake_glDrawArrays(unsigned mode, int first, int count)
{
  (void)first;
  log_call("glDrawArrays", (int)mode, count, fake.gl.program, fake.gl.framebuffer);
}

#define FAKE_GL(name) {#name, (void *)fake_##name}
static const struct {
  const char *name;
  void *function;
} gl_functions[] = {
    FAKE_GL(glGetString),
    FAKE_GL(glGetIntegerv),
    FAKE_GL(glGetVertexAttribiv),
    FAKE_GL(glIsEnabled),
    FAKE_GL(glEnable),
    FAKE_GL(glDisable),
    FAKE_GL(glCreateShader),
    FAKE_GL(glShaderSource),
    FAKE_GL(glCompileShader),
    FAKE_GL(glGetShaderiv),
    FAKE_GL(glGetShaderInfoLog),
    FAKE_GL(glDeleteShader),
    FAKE_GL(glCreateProgram),
    FAKE_GL(glAttachShader),
    FAKE_GL(glBindAttribLocation),
    FAKE_GL(glLinkProgram),
    FAKE_GL(glGetProgramiv),
    FAKE_GL(glGetProgramInfoLog),
    FAKE_GL(glDeleteProgram),
    FAKE_GL(glUseProgram),
    FAKE_GL(glGetUniformLocation),
    FAKE_GL(glUniform1i),
    FAKE_GL(glUniform1f),
    FAKE_GL(glUniform2f),
    FAKE_GL(glUniformMatrix4fv),
    FAKE_GL(glGenTextures),
    FAKE_GL(glDeleteTextures),
    FAKE_GL(glBindTexture),
    FAKE_GL(glActiveTexture),
    FAKE_GL(glTexParameteri),
    FAKE_GL(glTexImage2D),
    FAKE_GL(glTexSubImage2D),
    FAKE_GL(glGenFramebuffers),
    FAKE_GL(glDeleteFramebuffers),
    FAKE_GL(glBindFramebuffer),
    FAKE_GL(glFramebufferTexture2D),
    FAKE_GL(glCheckFramebufferStatus),
    FAKE_GL(glViewport),
    FAKE_GL(glBindBuffer),
    FAKE_GL(glVertexAttribPointer),
    FAKE_GL(glEnableVertexAttribArray),
    FAKE_GL(glDisableVertexAttribArray),
    FAKE_GL(glDrawArrays),
};

void *SDL_GL_GetProcAddress(const char *name)
{
  size_t i;

  for (i = 0; i < sizeof gl_functions / sizeof *gl_functions; i++) {
    if (strcmp(gl_functions[i].name, name) == 0) {
      return gl_functions[i].function;
    }
  }
  return NULL;
}
