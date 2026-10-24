/*
 * NextUI's shaders, as minarch runs them: up to three passes of RetroArch
 * style .glsl files from /Shaders/glsl, each with a filter, a scale and the
 * sizes it is told it works from, and the presets in /Shaders that set them
 * all at once. The settings are saved in minarch's own key = value form, so a
 * preset is read exactly as the settings are.
 *
 * Nothing here calls SDL or GL: shader-gl.c runs the passes and this holds
 * what they are, reads and writes them, and works out their sizes.
 */

#ifndef SHADER_H
#define SHADER_H

#include <stddef.h>

/* minarch's three passes, and the parameters a shader may declare */
#define SHADER_PASSES 3
#define SHADER_MAX_PARAMS 32
/* the parameter values a settings file can hold, over every shader it has used */
#define SHADER_MAX_VALUES 64
#define SHADER_NAME_SIZE 128

enum shader_filter { SHADER_NEAREST, SHADER_LINEAR, SHADER_FILTER_COUNT };
/* what a pass's InputSize and TextureSize are: the game's frame or the pass before */
enum shader_size_type { SHADER_SOURCE, SHADER_RELATIVE, SHADER_SIZE_TYPE_COUNT };

/* the upscale choices, 1 to 8 times the pass before or the size of the screen */
#define SHADER_UPSCALE_COUNT 9
#define SHADER_UPSCALE_SCREEN 8

extern const char *const shader_filter_names[SHADER_FILTER_COUNT];
extern const char *const shader_size_type_names[SHADER_SIZE_TYPE_COUNT];
extern const char *const shader_upscale_names[SHADER_UPSCALE_COUNT];
/* off, then 1 to 3, as Number of Shaders shows them */
extern const char *const shader_pass_count_names[SHADER_PASSES + 1];

struct shader_pass {
  /* a file in the glsl folder */
  char program[SHADER_NAME_SIZE];
  int filter;
  int srctype;
  int scaletype;
  /* an index into shader_upscale_names */
  int upscale;
};

struct shader_value {
  char name[SHADER_NAME_SIZE];
  float value;
};

struct shader_settings {
  /* the preset last chosen, empty for none, kept when a pass is changed by hand as minarch does */
  char preset[SHADER_NAME_SIZE];
  /* 0 is off */
  int passes;
  /* how the last pass is scaled to the screen, minarch's Screen Sharpness */
  int scale_filter;
  struct shader_pass pass[SHADER_PASSES];
  /* the parameters set, by name, as minarch saves them */
  struct shader_value values[SHADER_MAX_VALUES];
  int value_count;
};

/* a #pragma parameter line: the uniform's name, what it is shown as, and its range */
struct shader_param {
  char name[SHADER_NAME_SIZE];
  char label[SHADER_NAME_SIZE];
  float def, min, max, step;
};

/*
 * Off, with nearest scaling to the screen, and the passes NextUI's system.cfg
 * starts with: stock.glsl at 2x, then 1x smoothed, then to the screen.
 */
void shader_defaults(struct shader_settings *settings);

/*
 * Reads minarch's keys from text, a preset's or a settings file's, over what
 * settings already holds: a preset leaves alone what it does not name, as in
 * minarch. Other minarch keys and a core's options are skipped, and any other
 * key with a number for its value is a parameter's.
 */
void shader_parse_config(struct shader_settings *settings, const char *text);

/* reads path over the defaults, which stand when it cannot be read */
void shader_read_settings(struct shader_settings *settings, const char *path);
/* returns 0 when path cannot be written */
int shader_write_settings(const struct shader_settings *settings, const char *path);

/* reads the preset name in dir over settings and remembers it was chosen; 0 if it cannot be read */
int shader_apply_preset(struct shader_settings *settings, const char *dir, const char *name);

/* a parameter's value, as set or its default, kept within its range */
float shader_param_value(const struct shader_settings *settings, const struct shader_param *param);
void shader_set_value(struct shader_settings *settings, const char *name, float value);

/* how many values a parameter steps through, and which of them value is */
int shader_param_steps(const struct shader_param *param);
int shader_param_index(const struct shader_param *param, float value);
float shader_param_at(const struct shader_param *param, int index);

/*
 * The parameters a shader's source declares, as minarch reads them: a line
 * with fewer than its six fields is not one. Returns how many were found.
 */
int shader_parse_params(const char *source, struct shader_param *params, int max);

/*
 * The parameters of every pass in use, each name once, read from the files
 * in glsl_dir. Returns how many were found.
 */
int shader_load_params(const struct shader_settings *settings, const char *glsl_dir,
                       struct shader_param *params, int max);

enum shader_stage { SHADER_VERTEX, SHADER_FRAGMENT };

/*
 * The source as minarch hands it to GL: without its parameter lines, with
 * VERTEX or FRAGMENT defined after any #version, and with a precision for the
 * fragment stage. A desktop #version becomes 300 es where the context has GLSL
 * ES 3, and 100 where it does not, as do sources that name none. The result
 * is malloc'd, and NULL only when memory runs out.
 */
char *shader_wrap_source(const char *source, enum shader_stage stage, int es3);

/* what a pass draws to and the sizes it is told about, worked out as minarch does */
struct shader_pass_size {
  int output_w, output_h;
  int input_w, input_h;
  int texture_w, texture_h;
};

/*
 * Each pass in use, from the game's frame of orig_w by orig_h, for a screen
 * rect of screen_w by screen_h. Nothing is bigger than max_size.
 */
void shader_pass_sizes(const struct shader_settings *settings, int orig_w, int orig_h, int screen_w,
                       int screen_h, int max_size, struct shader_pass_size *sizes);

/*
 * The files in dir ending in suffix, sorted, without dot files, which also
 * skips the ._ files macOS leaves on FAT cards. Returns how many, with names
 * malloc'd, or 0 when the folder cannot be read.
 */
int shader_list_files(const char *dir, const char *suffix, char ***names);

/* reads a whole file, malloc'd and terminated, or NULL */
char *shader_read_file(const char *path);

#endif
