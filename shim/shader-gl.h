/*
 * Runs minarch's shader passes over pico-8's frame with the GL context of
 * SDL's own GLES renderer. SDL keeps track of the GL state it last set rather
 * than asking for it, so everything this changes is put back as it was, and
 * SDL's renderer carries on drawing the overlay and the menu over the result.
 *
 * GL is reached through SDL_GL_GetProcAddress, which sdl-display.c passes in,
 * so the shim links nothing new. Like the rest of the shim, nothing here
 * takes anything from libm.
 */

#ifndef SHADER_GL_H
#define SHADER_GL_H

#include <stdint.h>

#include "shader.h"

/*
 * Looks up every GL function the passes need and builds the copy to the
 * screen. Returns 0 when the context cannot run them, which leaves pico-8's
 * own copy of the frame in place.
 */
int shader_gl_init(void *(*get_proc)(const char *));

/* whether the context has GLSL ES 3, which a desktop #version becomes 300 es for */
int shader_gl_es3(void);

/* forgets every GL object, as SDL destroyed the context they lived in */
void shader_gl_reset(void);

/*
 * Draws the frame, w by h pixels in RGBA byte order from the top row down,
 * through every pass in use and onto the screen rect at x, y, from the top
 * left, of a screen screen_h tall. Returns 0 when nothing was drawn. A pass
 * whose shader will not build passes its input through unchanged, and the
 * first time it is tried its file is named in *failed.
 */
int shader_gl_draw(const struct shader_settings *settings, const char *glsl_dir,
                   const uint8_t *rgba, int w, int h, int x, int y, int dst_w, int dst_h,
                   int screen_h, const char **failed);

#endif
