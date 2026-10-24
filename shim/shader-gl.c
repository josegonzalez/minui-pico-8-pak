/*
 * minarch's shader passes in SDL's GL context. See shader-gl.h.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "shader-gl.h"

#define LOG(...) gfx_log(__VA_ARGS__)

/* From GLES2/gl2.h. */
typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef unsigned char GLboolean;
typedef char GLchar;
#define GL_FALSE 0
#define GL_TRIANGLE_STRIP 0x0005
#define GL_SCISSOR_TEST 0x0C11
#define GL_BLEND 0x0BE2
#define GL_VIEWPORT 0x0BA2
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_TEXTURE_2D 0x0DE1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_VERSION 0x1F02
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE0 0x84C0
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_VERTEX_ATTRIB_ARRAY_ENABLED 0x8622
#define GL_ARRAY_BUFFER 0x8892
#define GL_ARRAY_BUFFER_BINDING 0x8894
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_CURRENT_PROGRAM 0x8B8D
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER 0x8D40

/* the vertex arrays SDL's renderer may have on, which are all turned off while the passes draw */
#define SAVED_ATTRIBS 8
/* where the passes' VertexCoord and TexCoord are bound */
#define ATTRIB_VERTEX 0
#define ATTRIB_TEXCOORD 1

#define GL_FUNCTIONS(F)                                                                            \
  F(const unsigned char *, GetString, (GLenum))                                                    \
  F(void, GetIntegerv, (GLenum, GLint *))                                                          \
  F(void, GetVertexAttribiv, (GLuint, GLenum, GLint *))                                            \
  F(GLboolean, IsEnabled, (GLenum))                                                                \
  F(void, Enable, (GLenum))                                                                        \
  F(void, Disable, (GLenum))                                                                       \
  F(GLuint, CreateShader, (GLenum))                                                                \
  F(void, ShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *))                   \
  F(void, CompileShader, (GLuint))                                                                 \
  F(void, GetShaderiv, (GLuint, GLenum, GLint *))                                                  \
  F(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))                                \
  F(void, DeleteShader, (GLuint))                                                                  \
  F(GLuint, CreateProgram, (void))                                                                 \
  F(void, AttachShader, (GLuint, GLuint))                                                          \
  F(void, BindAttribLocation, (GLuint, GLuint, const GLchar *))                                    \
  F(void, LinkProgram, (GLuint))                                                                   \
  F(void, GetProgramiv, (GLuint, GLenum, GLint *))                                                 \
  F(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))                               \
  F(void, DeleteProgram, (GLuint))                                                                 \
  F(void, UseProgram, (GLuint))                                                                    \
  F(GLint, GetUniformLocation, (GLuint, const GLchar *))                                           \
  F(void, Uniform1i, (GLint, GLint))                                                               \
  F(void, Uniform1f, (GLint, GLfloat))                                                             \
  F(void, Uniform2f, (GLint, GLfloat, GLfloat))                                                    \
  F(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *))                          \
  F(void, GenTextures, (GLsizei, GLuint *))                                                        \
  F(void, DeleteTextures, (GLsizei, const GLuint *))                                               \
  F(void, BindTexture, (GLenum, GLuint))                                                           \
  F(void, ActiveTexture, (GLenum))                                                                 \
  F(void, TexParameteri, (GLenum, GLenum, GLint))                                                  \
  F(void, TexImage2D,                                                                              \
    (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *))                 \
  F(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,          \
                          const void *))                                                           \
  F(void, GenFramebuffers, (GLsizei, GLuint *))                                                    \
  F(void, DeleteFramebuffers, (GLsizei, const GLuint *))                                           \
  F(void, BindFramebuffer, (GLenum, GLuint))                                                       \
  F(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))                           \
  F(GLenum, CheckFramebufferStatus, (GLenum))                                                      \
  F(void, Viewport, (GLint, GLint, GLsizei, GLsizei))                                              \
  F(void, BindBuffer, (GLenum, GLuint))                                                            \
  F(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *))          \
  F(void, EnableVertexAttribArray, (GLuint))                                                       \
  F(void, DisableVertexAttribArray, (GLuint))                                                      \
  F(void, DrawArrays, (GLenum, GLint, GLsizei))

#define DECLARE(ret, name, args) ret(*name) args;
static struct {
  GL_FUNCTIONS(DECLARE)
} gl;

/* a program and where its inputs are, -1 for those it does not use */
struct program {
  GLuint id;
  GLint mvp, frame_direction, frame_count, output_size, texture_size, input_size;
  GLint orig_texture_size, orig_input_size, texture, orig_texture, texel_size, flip;
  struct shader_param params[SHADER_MAX_PARAMS];
  GLint param_locations[SHADER_MAX_PARAMS];
  int param_count;
};

/* a pass's program, built from file when it changes, and the texture it draws into */
static struct pass {
  char file[SHADER_NAME_SIZE];
  int tried, failed;
  struct program program;
  GLuint target;
  int target_w, target_h, target_filter;
} passes[SHADER_PASSES];

static int ready, es3, max_size;
/* the copy to the screen, flipped as minarch's default.glsl is, and unflipped for a broken pass */
static struct program copy;
static GLuint orig_texture, framebuffer;
static int orig_w, orig_h;
static unsigned frame_count;

static const char copy_vertex[] = "#version 100\n"
                                  "attribute vec4 VertexCoord;\n"
                                  "attribute vec4 TexCoord;\n"
                                  "uniform float Flip;\n"
                                  "varying vec2 uv;\n"
                                  "void main() {\n"
                                  "  gl_Position = VertexCoord;\n"
                                  "  uv = vec2(TexCoord.x, Flip > 0.5 ? 1.0 - TexCoord.y : TexCoord.y);\n"
                                  "}\n";
static const char copy_fragment[] = "#version 100\n"
                                    "precision mediump float;\n"
                                    "uniform sampler2D Texture;\n"
                                    "varying vec2 uv;\n"
                                    "void main() {\n"
                                    "  gl_FragColor = texture2D(Texture, uv);\n"
                                    "}\n";

static GLuint compile(GLenum type, const char *source, const char *name)
{
  GLuint shader = gl.CreateShader(type);
  GLint compiled = 0;
  char log[512];

  if (shader == 0) {
    return 0;
  }
  gl.ShaderSource(shader, 1, &source, NULL);
  gl.CompileShader(shader);
  gl.GetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    log[0] = '\0';
    gl.GetShaderInfoLog(shader, sizeof log, NULL, log);
    LOG("cannot build the %s shader of %s: %s\n", type == GL_VERTEX_SHADER ? "vertex" : "fragment",
        name, log);
    gl.DeleteShader(shader);
    return 0;
  }
  return shader;
}

/*
 * Links the two stages, with the attributes where the passes draw from, and
 * finds the uniforms, the parameters program already holds among them.
 */
static int link(struct program *program, const char *vertex, const char *fragment, const char *name)
{
  GLuint vs = compile(GL_VERTEX_SHADER, vertex, name);
  GLuint fs = vs != 0 ? compile(GL_FRAGMENT_SHADER, fragment, name) : 0;
  GLint linked = 0;
  char log[512];
  int i;

  program->id = 0;
  if (fs == 0) {
    if (vs != 0) {
      gl.DeleteShader(vs);
    }
    return 0;
  }

  program->id = gl.CreateProgram();
  gl.AttachShader(program->id, vs);
  gl.AttachShader(program->id, fs);
  gl.BindAttribLocation(program->id, ATTRIB_VERTEX, "VertexCoord");
  gl.BindAttribLocation(program->id, ATTRIB_TEXCOORD, "TexCoord");
  gl.LinkProgram(program->id);
  gl.DeleteShader(vs);
  gl.DeleteShader(fs);
  gl.GetProgramiv(program->id, GL_LINK_STATUS, &linked);
  if (!linked) {
    log[0] = '\0';
    gl.GetProgramInfoLog(program->id, sizeof log, NULL, log);
    LOG("cannot link the shaders of %s: %s\n", name, log);
    gl.DeleteProgram(program->id);
    program->id = 0;
    return 0;
  }

  program->mvp = gl.GetUniformLocation(program->id, "MVPMatrix");
  program->frame_direction = gl.GetUniformLocation(program->id, "FrameDirection");
  program->frame_count = gl.GetUniformLocation(program->id, "FrameCount");
  program->output_size = gl.GetUniformLocation(program->id, "OutputSize");
  program->texture_size = gl.GetUniformLocation(program->id, "TextureSize");
  program->input_size = gl.GetUniformLocation(program->id, "InputSize");
  program->orig_texture_size = gl.GetUniformLocation(program->id, "OrigTextureSize");
  program->orig_input_size = gl.GetUniformLocation(program->id, "OrigInputSize");
  program->texture = gl.GetUniformLocation(program->id, "Texture");
  program->orig_texture = gl.GetUniformLocation(program->id, "OrigTexture");
  program->texel_size = gl.GetUniformLocation(program->id, "texelSize");
  program->flip = gl.GetUniformLocation(program->id, "Flip");
  for (i = 0; i < program->param_count; i++) {
    program->param_locations[i] = gl.GetUniformLocation(program->id, program->params[i].name);
  }
  return 1;
}

int shader_gl_init(void *(*get_proc)(const char *))
{
  const char *version;
  void *proc;

  if (ready) {
    return 1;
  }

#define LOAD(ret, name, args)                                                                      \
  proc = get_proc("gl" #name);                                                                     \
  if (proc == NULL) {                                                                              \
    LOG("this GL has no gl" #name ", not drawing shaders\n");                                      \
    return 0;                                                                                      \
  }                                                                                                \
  memcpy(&gl.name, &proc, sizeof proc);
  GL_FUNCTIONS(LOAD)
#undef LOAD

  version = (const char *)gl.GetString(GL_VERSION);
  if (version == NULL) {
    LOG("cannot read the GL version, not drawing shaders\n");
    return 0;
  }
  es3 = strncmp(version, "OpenGL ES ", 10) == 0 && version[10] >= '3' && version[10] <= '9';
  gl.GetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
  LOG("drawing shaders with %s, up to %dx%d\n", version, max_size, max_size);

  memset(&copy, 0, sizeof copy);
  if (!link(&copy, copy_vertex, copy_fragment, "the copy to the screen")) {
    return 0;
  }
  ready = 1;
  return 1;
}

int shader_gl_es3(void)
{
  return es3;
}

void shader_gl_reset(void)
{
  memset(passes, 0, sizeof passes);
  memset(&copy, 0, sizeof copy);
  orig_texture = 0;
  framebuffer = 0;
  orig_w = orig_h = 0;
  ready = 0;
}

static void drop_program(struct pass *pass)
{
  if (pass->program.id != 0) {
    gl.DeleteProgram(pass->program.id);
  }
  memset(&pass->program, 0, sizeof pass->program);
}

/* builds a pass's program again when its file changes, or when the last try failed and it was changed back */
static void prepare_pass(struct pass *pass, const char *file, const char *glsl_dir,
                         const char **failed)
{
  char path[4096];
  char *source, *vertex, *fragment;
  int built = 0;

  if (pass->tried && strcmp(pass->file, file) == 0) {
    return;
  }

  drop_program(pass);
  snprintf(pass->file, sizeof pass->file, "%s", file);
  pass->tried = 1;

  snprintf(path, sizeof path, "%s/%s", glsl_dir != NULL ? glsl_dir : "", file);
  source = shader_read_file(path);
  if (source == NULL) {
    LOG("cannot read %s, passing that pass through\n", path);
  } else {
    vertex = shader_wrap_source(source, SHADER_VERTEX, es3);
    fragment = shader_wrap_source(source, SHADER_FRAGMENT, es3);
    pass->program.param_count = shader_parse_params(source, pass->program.params, SHADER_MAX_PARAMS);
    built = vertex != NULL && fragment != NULL && link(&pass->program, vertex, fragment, file);
    free(vertex);
    free(fragment);
    free(source);
  }

  pass->failed = !built;
  if (built) {
    LOG("built the shader %s\n", file);
  } else if (failed != NULL && *failed == NULL) {
    *failed = pass->file;
  }
}

static void set_filter(GLuint texture, int filter)
{
  GLint value = filter == SHADER_LINEAR ? GL_LINEAR : GL_NEAREST;

  gl.BindTexture(GL_TEXTURE_2D, texture);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, value);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, value);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

/* a pass's texture, made again at a new size or for a new filter */
static void prepare_target(struct pass *pass, int w, int h, int filter)
{
  if (pass->target != 0 && pass->target_w == w && pass->target_h == h &&
      pass->target_filter == filter) {
    return;
  }
  if (pass->target == 0) {
    gl.GenTextures(1, &pass->target);
  }
  set_filter(pass->target, filter);
  if (pass->target_w != w || pass->target_h != h) {
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  }
  pass->target_w = w;
  pass->target_h = h;
  pass->target_filter = filter;
}

/*
 * minarch's quad: the top left corner samples the bottom of the texture, so
 * the passes work upside down from the frame's rows and only the copy to the
 * screen turns it the right way up.
 */
static void draw_quad(void)
{
  static const GLfloat vertices[] = {-1, 1, 0, 1, -1, -1, 0, 1, 1, 1, 0, 1, 1, -1, 0, 1};
  static const GLfloat texcoords[] = {0, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0};

  gl.VertexAttribPointer(ATTRIB_VERTEX, 4, GL_FLOAT, GL_FALSE, 0, vertices);
  gl.VertexAttribPointer(ATTRIB_TEXCOORD, 4, GL_FLOAT, GL_FALSE, 0, texcoords);
  gl.EnableVertexAttribArray(ATTRIB_VERTEX);
  gl.EnableVertexAttribArray(ATTRIB_TEXCOORD);
  gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  gl.DisableVertexAttribArray(ATTRIB_VERTEX);
  gl.DisableVertexAttribArray(ATTRIB_TEXCOORD);
}

static void set_size(GLint location, int w, int h)
{
  if (location >= 0) {
    gl.Uniform2f(location, (GLfloat)w, (GLfloat)h);
  }
}

/* gives a program what minarch gives every pass, and draws input into the viewport */
static void run_program(const struct program *program, const struct shader_settings *settings,
                        GLuint input, const struct shader_pass_size *size, int flip)
{
  static const GLfloat identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  int i;

  gl.UseProgram(program->id);
  if (program->mvp >= 0) {
    gl.UniformMatrix4fv(program->mvp, 1, GL_FALSE, identity);
  }
  if (program->frame_direction >= 0) {
    gl.Uniform1i(program->frame_direction, 1);
  }
  if (program->frame_count >= 0) {
    gl.Uniform1i(program->frame_count, (GLint)frame_count);
  }
  set_size(program->output_size, size->output_w, size->output_h);
  set_size(program->texture_size, size->texture_w, size->texture_h);
  set_size(program->input_size, size->input_w, size->input_h);
  set_size(program->orig_texture_size, orig_w, orig_h);
  set_size(program->orig_input_size, orig_w, orig_h);
  if (program->texel_size >= 0) {
    gl.Uniform2f(program->texel_size, 1.0f / (size->texture_w > 0 ? size->texture_w : 1),
                 1.0f / (size->texture_h > 0 ? size->texture_h : 1));
  }
  if (program->flip >= 0) {
    gl.Uniform1f(program->flip, flip ? 1.0f : 0.0f);
  }
  for (i = 0; i < program->param_count; i++) {
    if (program->param_locations[i] >= 0) {
      gl.Uniform1f(program->param_locations[i], shader_param_value(settings, &program->params[i]));
    }
  }

  if (program->orig_texture >= 0) {
    gl.Uniform1i(program->orig_texture, 1);
    gl.ActiveTexture(GL_TEXTURE0 + 1);
    gl.BindTexture(GL_TEXTURE_2D, orig_texture);
    gl.ActiveTexture(GL_TEXTURE0);
  }
  if (program->texture >= 0) {
    gl.Uniform1i(program->texture, 0);
  }
  gl.BindTexture(GL_TEXTURE_2D, input);
  draw_quad();
}

/* SDL's renderer keeps its own record of these and never asks GL again */
struct gl_state {
  GLint program, active_texture, texture0, texture1, array_buffer, framebuffer;
  GLint viewport[4];
  GLint attribs[SAVED_ATTRIBS];
  GLboolean blend, scissor;
};

static void save_state(struct gl_state *saved)
{
  int i;

  gl.GetIntegerv(GL_CURRENT_PROGRAM, &saved->program);
  gl.GetIntegerv(GL_ACTIVE_TEXTURE, &saved->active_texture);
  gl.ActiveTexture(GL_TEXTURE0 + 1);
  gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &saved->texture1);
  gl.ActiveTexture(GL_TEXTURE0);
  gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &saved->texture0);
  gl.GetIntegerv(GL_ARRAY_BUFFER_BINDING, &saved->array_buffer);
  gl.GetIntegerv(GL_FRAMEBUFFER_BINDING, &saved->framebuffer);
  gl.GetIntegerv(GL_VIEWPORT, saved->viewport);
  for (i = 0; i < SAVED_ATTRIBS; i++) {
    saved->attribs[i] = 0;
    gl.GetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &saved->attribs[i]);
  }
  saved->blend = gl.IsEnabled(GL_BLEND);
  saved->scissor = gl.IsEnabled(GL_SCISSOR_TEST);
}

static void restore_state(const struct gl_state *saved)
{
  int i;

  for (i = 0; i < SAVED_ATTRIBS; i++) {
    if (saved->attribs[i]) {
      gl.EnableVertexAttribArray(i);
    } else {
      gl.DisableVertexAttribArray(i);
    }
  }
  gl.BindBuffer(GL_ARRAY_BUFFER, saved->array_buffer);
  gl.BindFramebuffer(GL_FRAMEBUFFER, saved->framebuffer);
  gl.Viewport(saved->viewport[0], saved->viewport[1], saved->viewport[2], saved->viewport[3]);
  gl.ActiveTexture(GL_TEXTURE0 + 1);
  gl.BindTexture(GL_TEXTURE_2D, saved->texture1);
  gl.ActiveTexture(GL_TEXTURE0);
  gl.BindTexture(GL_TEXTURE_2D, saved->texture0);
  gl.ActiveTexture(saved->active_texture);
  gl.UseProgram(saved->program);
  if (saved->blend) {
    gl.Enable(GL_BLEND);
  } else {
    gl.Disable(GL_BLEND);
  }
  if (saved->scissor) {
    gl.Enable(GL_SCISSOR_TEST);
  } else {
    gl.Disable(GL_SCISSOR_TEST);
  }
}

int shader_gl_draw(const struct shader_settings *settings, const char *glsl_dir,
                   const uint8_t *rgba, int w, int h, int x, int y, int dst_w, int dst_h,
                   int screen_h, const char **failed)
{
  struct shader_pass_size sizes[SHADER_PASSES], final;
  struct gl_state saved;
  GLuint input;
  int count = settings->passes, i;

  if (!ready || count <= 0 || w <= 0 || h <= 0 || dst_w <= 0 || dst_h <= 0) {
    return 0;
  }
  count = count > SHADER_PASSES ? SHADER_PASSES : count;

  save_state(&saved);
  gl.Disable(GL_BLEND);
  gl.Disable(GL_SCISSOR_TEST);
  gl.BindBuffer(GL_ARRAY_BUFFER, 0);
  for (i = 0; i < SAVED_ATTRIBS; i++) {
    gl.DisableVertexAttribArray(i);
  }

  for (i = 0; i < count; i++) {
    prepare_pass(&passes[i], settings->pass[i].program, glsl_dir, failed);
  }

  /* the frame, filtered as the first pass reads it */
  if (orig_texture == 0) {
    gl.GenTextures(1, &orig_texture);
  }
  set_filter(orig_texture, settings->pass[0].filter);
  if (w != orig_w || h != orig_h) {
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    orig_w = w;
    orig_h = h;
  } else {
    gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  }

  if (framebuffer == 0) {
    gl.GenFramebuffers(1, &framebuffer);
  }
  gl.BindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  shader_pass_sizes(settings, w, h, dst_w, dst_h, max_size, sizes);
  input = orig_texture;
  for (i = 0; i < count; i++) {
    struct pass *pass = &passes[i];
    int next_filter = i + 1 < count ? settings->pass[i + 1].filter : settings->scale_filter;

    prepare_target(pass, sizes[i].output_w, sizes[i].output_h, next_filter);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pass->target, 0);
    if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      LOG("cannot draw into a %dx%d texture, not drawing shaders\n", sizes[i].output_w,
          sizes[i].output_h);
      restore_state(&saved);
      return 0;
    }
    gl.Viewport(0, 0, sizes[i].output_w, sizes[i].output_h);
    run_program(pass->failed ? &copy : &pass->program, settings, input, &sizes[i], 0);
    input = pass->target;
  }

  /* the last pass to the screen, which is SDL's framebuffer, measured from the bottom */
  gl.BindFramebuffer(GL_FRAMEBUFFER, saved.framebuffer);
  gl.Viewport(x, screen_h - y - dst_h, dst_w, dst_h);
  final.output_w = final.input_w = final.texture_w = dst_w;
  final.output_h = final.input_h = final.texture_h = dst_h;
  run_program(&copy, settings, input, &final, 1);

  restore_state(&saved);
  frame_count++;
  return 1;
}
