/*
 * Checks how shader.c reads NextUI's presets and shaders, linked straight
 * against it with no SDL or GL at all:
 *
 *   shader-test
 *
 * Prints TAP and exits non-zero on a failure.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../shader.h"

static int tests;
static int failures;
static char dir[] = "/tmp/shader-test.XXXXXX";

static void check(int passed, const char *name)
{
  tests++;
  if (!passed) {
    failures++;
  }
  printf("%s %d - %s\n", passed ? "ok" : "not ok", tests, name);
}

static void write_file(const char *path, const char *text)
{
  FILE *file = fopen(path, "w");

  if (file == NULL || fputs(text, file) < 0) {
    perror(path);
    exit(2);
  }
  fclose(file);
}

static int near(float a, float b)
{
  return a - b < 0.001f && b - a < 0.001f;
}

/* whether text has needle at its very start */
static int starts(const char *text, const char *needle)
{
  return text != NULL && strncmp(text, needle, strlen(needle)) == 0;
}

static void test_defaults(void)
{
  struct shader_settings settings;

  shader_defaults(&settings);
  check(settings.passes == 0 && settings.preset[0] == '\0' &&
            settings.scale_filter == SHADER_NEAREST && settings.value_count == 0,
        "the shaders start off, with nearest scaling to the screen");
  check(strcmp(settings.pass[0].program, "stock.glsl") == 0 && settings.pass[0].upscale == 1 &&
            settings.pass[1].filter == SHADER_LINEAR && settings.pass[1].srctype == SHADER_RELATIVE &&
            settings.pass[2].upscale == SHADER_UPSCALE_SCREEN,
        "with the passes NextUI's system.cfg sets up");
}

/* NextUI's own presets, which end without a newline and carry keys that are not shaders' */
static void test_presets(void)
{
  struct shader_settings settings;
  char path[8192];

  shader_defaults(&settings);
  shader_parse_config(&settings, "minarch_nrofshaders = 2\n"
                                 "minarch_shader1 = pixellate.glsl\n"
                                 "minarch_shader1_filter = LINEAR\n"
                                 "minarch_shader1_srctype = relative\n"
                                 "minarch_shader1_scaletype = relative\n"
                                 "minarch_shader1_upscale = screen\n"
                                 "minarch_shader2 = lcd3x.glsl\n"
                                 "minarch_shader2_upscale = 3\n"
                                 "minarch_scale_filter = LINEAR\n"
                                 "gambatte_gb_internal_palette = TWB64 - Pack 2\n"
                                 "minarch_screen_effect = 1\n"
                                 "autoscale = 1.00");
  check(settings.passes == 2 && strcmp(settings.pass[0].program, "pixellate.glsl") == 0 &&
            settings.pass[0].filter == SHADER_LINEAR && settings.pass[0].srctype == SHADER_RELATIVE &&
            settings.pass[0].scaletype == SHADER_RELATIVE &&
            settings.pass[0].upscale == SHADER_UPSCALE_SCREEN,
        "a preset sets a pass's shader, filter, sizes and scale");
  check(strcmp(settings.pass[1].program, "lcd3x.glsl") == 0 && settings.pass[1].upscale == 2 &&
            settings.pass[1].filter == SHADER_LINEAR,
        "and leaves alone what it does not name, as minarch reads it over what is there");
  check(settings.scale_filter == SHADER_LINEAR, "it sets Screen Sharpness too");
  check(settings.value_count == 1 && strcmp(settings.values[0].name, "autoscale") == 0 &&
            near(settings.values[0].value, 1.0f),
        "a number under any other key is a parameter, and a core's option is not");

  shader_parse_config(&settings, "minarch_nrofshaders = off\r\nminarch_shader3_upscale=8\r\n"
                                 "minarch_shader4 = nowhere.glsl\r\nnot a setting\r\n"
                                 "minarch_shader1_filter = BLURRY\r\n");
  check(settings.passes == 0 && settings.pass[2].upscale == 7 &&
            settings.pass[0].filter == SHADER_LINEAR,
        "CRLF lines and no spaces read, and a fourth pass or an unknown value changes nothing");
  shader_parse_config(&settings, "minarch_nrofshaders = 0");
  check(settings.passes == 0, "a number of shaders of 0 is off");

  snprintf(path, sizeof path, "%s/scanlines.cfg", dir);
  write_file(path, "minarch_nrofshaders = 1\nminarch_shader1 = res-independent-scanlines.glsl\n");
  shader_defaults(&settings);
  check(shader_apply_preset(&settings, dir, "scanlines.cfg") && settings.passes == 1 &&
            strcmp(settings.preset, "scanlines.cfg") == 0 &&
            strcmp(settings.pass[0].program, "res-independent-scanlines.glsl") == 0,
        "a preset applied from its folder is remembered by name");
  check(!shader_apply_preset(&settings, dir, "missing.cfg") &&
            strcmp(settings.preset, "scanlines.cfg") == 0,
        "one that cannot be read changes nothing");
}

static void test_settings_file(void)
{
  struct shader_settings settings, read_back;
  char path[8192];

  snprintf(path, sizeof path, "%s/shaders", dir);
  shader_read_settings(&read_back, path);
  check(read_back.passes == 0 && strcmp(read_back.pass[0].program, "stock.glsl") == 0,
        "no settings file reads as the defaults");

  shader_defaults(&settings);
  snprintf(settings.preset, sizeof settings.preset, "old-tv.cfg");
  settings.passes = 3;
  settings.scale_filter = SHADER_LINEAR;
  snprintf(settings.pass[2].program, sizeof settings.pass[2].program, "lcd1x.glsl");
  settings.pass[2].scaletype = SHADER_RELATIVE;
  settings.pass[1].upscale = 5;
  shader_set_value(&settings, "BLOOM", 0.25f);
  shader_set_value(&settings, "MASK", 2.0f);
  shader_set_value(&settings, "BLOOM", 0.75f);
  check(settings.value_count == 2 && near(settings.values[0].value, 0.75f),
        "setting a parameter again replaces its value");
  check(shader_write_settings(&settings, path), "the settings are written");

  shader_read_settings(&read_back, path);
  check(strcmp(read_back.preset, "old-tv.cfg") == 0 && read_back.passes == 3 &&
            read_back.scale_filter == SHADER_LINEAR &&
            strcmp(read_back.pass[2].program, "lcd1x.glsl") == 0 &&
            read_back.pass[2].scaletype == SHADER_RELATIVE && read_back.pass[1].upscale == 5 &&
            read_back.value_count == 2 && strcmp(read_back.values[1].name, "MASK") == 0 &&
            near(read_back.values[0].value, 0.75f),
        "and read back the same");
  check(!shader_write_settings(&settings, NULL), "no path writes nothing");
}

static void test_params(void)
{
  static const char source[] =
      "// a comment\n"
      "#pragma parameter SCANLINE \"Scanline Strength\" 0.5 0.0 1.0 0.05\n"
      "#pragma parameter  MASK \"Mask\" 2.0 0.0 3.0 1.0\r\n"
      "#pragma parameter NOSTEP \"No Step\" 1.0 0.0 2.0\n"
      "#pragma parameter BAD\n"
      "uniform float SCANLINE;\n";
  struct shader_param params[SHADER_MAX_PARAMS];
  struct shader_settings settings;
  int count = shader_parse_params(source, params, SHADER_MAX_PARAMS);

  check(count == 2 && strcmp(params[0].name, "SCANLINE") == 0 &&
            strcmp(params[0].label, "Scanline Strength") == 0 && near(params[0].def, 0.5f) &&
            near(params[0].step, 0.05f) && strcmp(params[1].name, "MASK") == 0 &&
            near(params[1].max, 3.0f),
        "a shader's parameters are read, and lines without all six fields are not, as in minarch");
  check(shader_parse_params(source, params, 1) == 1, "and no more than asked for");

  check(shader_param_steps(&params[0]) == 21 && shader_param_steps(&params[1]) == 4,
        "a parameter steps from its minimum to its maximum");
  check(shader_param_index(&params[0], 0.5f) == 10 && near(shader_param_at(&params[0], 10), 0.5f) &&
            near(shader_param_at(&params[0], 20), 1.0f),
        "and its values convert to steps and back");
  check(shader_param_index(&params[0], -3.0f) == 0 && shader_param_index(&params[0], 9.0f) == 20,
        "a value out of range is the nearest end");

  shader_defaults(&settings);
  check(near(shader_param_value(&settings, &params[0]), 0.5f), "an unset parameter is its default");
  shader_set_value(&settings, "SCANLINE", 0.2f);
  shader_set_value(&settings, "MASK", 9.0f);
  check(near(shader_param_value(&settings, &params[0]), 0.2f) &&
            near(shader_param_value(&settings, &params[1]), 3.0f),
        "a set one is its value, kept within its range");
}

static void test_load_params(void)
{
  struct shader_param params[SHADER_MAX_PARAMS];
  struct shader_settings settings;
  char glsl[8192], path[8300];

  snprintf(glsl, sizeof glsl, "%s/glsl", dir);
  mkdir(glsl, 0755);
  snprintf(path, sizeof path, "%s/one.glsl", glsl);
  write_file(path, "#pragma parameter A \"A\" 1.0 0.0 2.0 1.0\n"
                   "#pragma parameter B \"B\" 1.0 0.0 2.0 1.0\n");
  snprintf(path, sizeof path, "%s/two.glsl", glsl);
  write_file(path, "#pragma parameter B \"B\" 1.0 0.0 2.0 1.0\n"
                   "#pragma parameter C \"C\" 1.0 0.0 2.0 1.0\n");

  shader_defaults(&settings);
  snprintf(settings.pass[0].program, sizeof settings.pass[0].program, "one.glsl");
  snprintf(settings.pass[1].program, sizeof settings.pass[1].program, "two.glsl");
  snprintf(settings.pass[2].program, sizeof settings.pass[2].program, "missing.glsl");
  check(shader_load_params(&settings, glsl, params, SHADER_MAX_PARAMS) == 0,
        "with the shaders off there are no parameters");
  settings.passes = 3;
  check(shader_load_params(&settings, glsl, params, SHADER_MAX_PARAMS) == 3 &&
            strcmp(params[0].name, "A") == 0 && strcmp(params[1].name, "B") == 0 &&
            strcmp(params[2].name, "C") == 0,
        "the passes in use give their parameters, each name once, and a missing file none");
  settings.passes = 1;
  check(shader_load_params(&settings, glsl, params, SHADER_MAX_PARAMS) == 2,
        "and a pass not in use gives none");
}

static void test_wrap(void)
{
  char *wrapped;

  wrapped = shader_wrap_source("#pragma parameter A \"A\" 1.0 0.0 2.0 1.0\nvoid main() {}\n",
                               SHADER_VERTEX, 1);
  check(starts(wrapped, "#version 100\n#define VERTEX\nvoid main() {}\n"),
        "a shader without a version is GLSL ES 1.0, without its parameter lines");
  free(wrapped);

  wrapped = shader_wrap_source("// header\n#version 130\nvoid main() {}", SHADER_FRAGMENT, 1);
  check(starts(wrapped, "#version 300 es\n#define FRAGMENT\n#ifdef GL_ES\n") &&
            strstr(wrapped, "precision mediump float;") != NULL &&
            strstr(wrapped, "#define PARAMETER_UNIFORM\nvoid main() {}\n") != NULL &&
            strstr(wrapped, "#version 130") == NULL && strstr(wrapped, "// header") == NULL,
        "a desktop version becomes 300 es with GLSL ES 3, and the fragment stage gets a precision");
  free(wrapped);

  wrapped = shader_wrap_source("#version 110\nvoid main() {}\n", SHADER_VERTEX, 0);
  check(starts(wrapped, "#version 100\n#define VERTEX\nvoid main() {}\n"),
        "and 100 without it");
  free(wrapped);

  wrapped = shader_wrap_source("#version 300 es\nvoid main() {}\n", SHADER_VERTEX, 0);
  check(starts(wrapped, "#version 300 es\n#define VERTEX\nvoid main() {}\n"),
        "a version already for GLSL ES is kept as it is");
  free(wrapped);

  wrapped = shader_wrap_source("void main() {}\n// #version 110 in a comment after\n",
                               SHADER_VERTEX, 1);
  check(starts(wrapped, "#version 100\n#define VERTEX\n"),
        "only a version that starts its line counts");
  free(wrapped);
}

static void test_pass_sizes(void)
{
  struct shader_pass_size sizes[SHADER_PASSES];
  struct shader_settings settings;

  shader_defaults(&settings);
  settings.passes = 3;
  shader_pass_sizes(&settings, 128, 128, 768, 768, 4096, sizes);
  check(sizes[0].output_w == 256 && sizes[0].output_h == 256 && sizes[0].input_w == 128 &&
            sizes[0].texture_w == 128,
        "the first pass draws at its scale times the game's frame");
  check(sizes[1].output_w == 256 && sizes[1].input_w == 256 && sizes[1].texture_h == 256,
        "a relative pass is told the size of the pass before");
  check(sizes[2].output_w == 768 && sizes[2].output_h == 768 && sizes[2].input_w == 128 &&
            sizes[2].texture_w == 128,
        "a screen pass draws at the screen's size and a source one is told the game's");

  settings.pass[0].upscale = 7;
  settings.pass[1].upscale = 7;
  shader_pass_sizes(&settings, 128, 128, 768, 768, 2048, sizes);
  check(sizes[0].output_w == 1024 && sizes[1].output_w == 2048 && sizes[1].output_h == 2048,
        "nothing is bigger than GL can draw into");
}

static void test_list_files(void)
{
  char glsl[8192], path[8300];
  char **names;
  int count, i;

  snprintf(glsl, sizeof glsl, "%s/list", dir);
  mkdir(glsl, 0755);
  snprintf(path, sizeof path, "%s/zz.glsl", glsl);
  write_file(path, "");
  snprintf(path, sizeof path, "%s/aa.glsl", glsl);
  write_file(path, "");
  snprintf(path, sizeof path, "%s/._aa.glsl", glsl);
  write_file(path, "");
  snprintf(path, sizeof path, "%s/notes.txt", glsl);
  write_file(path, "");
  snprintf(path, sizeof path, "%s/.glsl", glsl);
  write_file(path, "");

  count = shader_list_files(glsl, ".glsl", &names);
  check(count == 2 && strcmp(names[0], "aa.glsl") == 0 && strcmp(names[1], "zz.glsl") == 0,
        "the shaders are listed sorted, without dot files or other files");
  for (i = 0; i < count; i++) {
    free(names[i]);
  }
  free(names);

  snprintf(path, sizeof path, "%s/missing", dir);
  check(shader_list_files(path, ".glsl", &names) == 0 && names == NULL &&
            shader_list_files(NULL, ".cfg", &names) == 0,
        "a missing folder has none");
}

int main(void)
{
  if (mkdtemp(dir) == NULL) {
    perror("mkdtemp");
    return 2;
  }

  test_defaults();
  test_presets();
  test_settings_file();
  test_params();
  test_load_params();
  test_wrap();
  test_pass_sizes();
  test_list_files();

  printf("1..%d\n", tests);
  return failures == 0 ? 0 : 1;
}
