/*
 * NextUI's shader settings and sources. See shader.h.
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shader.h"

const char *const shader_filter_names[SHADER_FILTER_COUNT] = {"NEAREST", "LINEAR"};
const char *const shader_size_type_names[SHADER_SIZE_TYPE_COUNT] = {"source", "relative"};
const char *const shader_upscale_names[SHADER_UPSCALE_COUNT] = {"1", "2", "3", "4",     "5",
                                                                "6", "7", "8", "screen"};
const char *const shader_pass_count_names[SHADER_PASSES + 1] = {"off", "1", "2", "3"};

/* minarch's key for a pass, numbered from 1, with what follows it, such as _filter */
#define PASS_KEY "minarch_shader"

void shader_defaults(struct shader_settings *settings)
{
  static const struct shader_pass passes[SHADER_PASSES] = {
      {"stock.glsl", SHADER_NEAREST, SHADER_SOURCE, SHADER_SOURCE, 1},
      {"stock.glsl", SHADER_LINEAR, SHADER_RELATIVE, SHADER_RELATIVE, 0},
      {"stock.glsl", SHADER_NEAREST, SHADER_SOURCE, SHADER_SOURCE, SHADER_UPSCALE_SCREEN},
  };

  memset(settings, 0, sizeof *settings);
  settings->scale_filter = SHADER_NEAREST;
  memcpy(settings->pass, passes, sizeof passes);
}

/* the index of value among count names, or -1 */
static int find_name(const char *const *names, int count, const char *value)
{
  int i;

  for (i = 0; i < count; i++) {
    if (strcmp(names[i], value) == 0) {
      return i;
    }
  }
  return -1;
}

/* whether text is a whole number, so a core option such as a palette's name is never a parameter */
static int parse_float(const char *text, float *value)
{
  char *end;

  if (*text == '\0') {
    return 0;
  }
  *value = strtof(text, &end);
  return *end == '\0';
}

void shader_set_value(struct shader_settings *settings, const char *name, float value)
{
  int i;

  for (i = 0; i < settings->value_count; i++) {
    if (strcmp(settings->values[i].name, name) == 0) {
      settings->values[i].value = value;
      return;
    }
  }
  if (settings->value_count < SHADER_MAX_VALUES) {
    snprintf(settings->values[settings->value_count].name, SHADER_NAME_SIZE, "%s", name);
    settings->values[settings->value_count].value = value;
    settings->value_count++;
  }
}

/* key is what follows minarch_shaderN, such as _filter, or empty for the program */
static void set_pass(struct shader_pass *pass, const char *key, const char *value)
{
  int found;

  if (*key == '\0') {
    snprintf(pass->program, sizeof pass->program, "%s", value);
  } else if (strcmp(key, "_filter") == 0) {
    found = find_name(shader_filter_names, SHADER_FILTER_COUNT, value);
    pass->filter = found >= 0 ? found : pass->filter;
  } else if (strcmp(key, "_srctype") == 0) {
    found = find_name(shader_size_type_names, SHADER_SIZE_TYPE_COUNT, value);
    pass->srctype = found >= 0 ? found : pass->srctype;
  } else if (strcmp(key, "_scaletype") == 0) {
    found = find_name(shader_size_type_names, SHADER_SIZE_TYPE_COUNT, value);
    pass->scaletype = found >= 0 ? found : pass->scaletype;
  } else if (strcmp(key, "_upscale") == 0) {
    found = find_name(shader_upscale_names, SHADER_UPSCALE_COUNT, value);
    pass->upscale = found >= 0 ? found : pass->upscale;
  }
}

static void set_key(struct shader_settings *settings, const char *key, const char *value)
{
  float number;
  int found;

  if (strcmp(key, "minarch_shaders_preset") == 0) {
    snprintf(settings->preset, sizeof settings->preset, "%s", value);
  } else if (strcmp(key, "minarch_nrofshaders") == 0) {
    found = find_name(shader_pass_count_names, SHADER_PASSES + 1, value);
    if (found < 0 && strcmp(value, "0") == 0) {
      found = 0;
    }
    settings->passes = found >= 0 ? found : settings->passes;
  } else if (strcmp(key, "minarch_scale_filter") == 0) {
    found = find_name(shader_filter_names, SHADER_FILTER_COUNT, value);
    settings->scale_filter = found >= 0 ? found : settings->scale_filter;
  } else if (strncmp(key, PASS_KEY, strlen(PASS_KEY)) == 0 &&
             key[strlen(PASS_KEY)] >= '1' && key[strlen(PASS_KEY)] <= '0' + SHADER_PASSES) {
    set_pass(&settings->pass[key[strlen(PASS_KEY)] - '1'], key + strlen(PASS_KEY) + 1, value);
  } else if (strncmp(key, "minarch_", 8) != 0 && parse_float(value, &number)) {
    shader_set_value(settings, key, number);
  }
}

/* the text between start and end without the spaces around it */
static void trimmed(const char *start, const char *end, char *out, size_t size)
{
  while (start < end && (*start == ' ' || *start == '\t')) {
    start++;
  }
  while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) {
    end--;
  }
  snprintf(out, size, "%.*s", (int)(end - start), start);
}

void shader_parse_config(struct shader_settings *settings, const char *text)
{
  char key[SHADER_NAME_SIZE], value[SHADER_NAME_SIZE];

  while (text != NULL && *text != '\0') {
    const char *end = text + strcspn(text, "\n");
    const char *equals = memchr(text, '=', end - text);

    if (equals != NULL) {
      trimmed(text, equals, key, sizeof key);
      trimmed(equals + 1, end, value, sizeof value);
      if (key[0] != '\0') {
        set_key(settings, key, value);
      }
    }
    text = *end == '\n' ? end + 1 : end;
  }
}

char *shader_read_file(const char *path)
{
  FILE *file = path != NULL ? fopen(path, "rb") : NULL;
  char *text = NULL;
  long size;

  if (file == NULL) {
    return NULL;
  }
  if (fseek(file, 0, SEEK_END) == 0 && (size = ftell(file)) >= 0 && fseek(file, 0, SEEK_SET) == 0 &&
      (text = malloc((size_t)size + 1)) != NULL) {
    if (fread(text, 1, (size_t)size, file) != (size_t)size) {
      free(text);
      text = NULL;
    } else {
      text[size] = '\0';
    }
  }
  fclose(file);
  return text;
}

void shader_read_settings(struct shader_settings *settings, const char *path)
{
  char *text = shader_read_file(path);

  shader_defaults(settings);
  shader_parse_config(settings, text);
  free(text);
}

int shader_write_settings(const struct shader_settings *settings, const char *path)
{
  FILE *file;
  int i;

  if (path == NULL || *path == '\0' || (file = fopen(path, "w")) == NULL) {
    return 0;
  }

  fprintf(file, "minarch_shaders_preset = %s\n", settings->preset);
  fprintf(file, "minarch_nrofshaders = %s\n", shader_pass_count_names[settings->passes]);
  fprintf(file, "minarch_scale_filter = %s\n", shader_filter_names[settings->scale_filter]);
  for (i = 0; i < SHADER_PASSES; i++) {
    const struct shader_pass *pass = &settings->pass[i];

    fprintf(file, PASS_KEY "%d = %s\n", i + 1, pass->program);
    fprintf(file, PASS_KEY "%d_filter = %s\n", i + 1, shader_filter_names[pass->filter]);
    fprintf(file, PASS_KEY "%d_srctype = %s\n", i + 1, shader_size_type_names[pass->srctype]);
    fprintf(file, PASS_KEY "%d_scaletype = %s\n", i + 1, shader_size_type_names[pass->scaletype]);
    fprintf(file, PASS_KEY "%d_upscale = %s\n", i + 1, shader_upscale_names[pass->upscale]);
  }
  for (i = 0; i < settings->value_count; i++) {
    fprintf(file, "%s = %.2f\n", settings->values[i].name, settings->values[i].value);
  }

  return fclose(file) == 0;
}

int shader_apply_preset(struct shader_settings *settings, const char *dir, const char *name)
{
  char path[4096];
  char *text;

  snprintf(path, sizeof path, "%s/%s", dir != NULL ? dir : "", name);
  text = shader_read_file(path);
  if (text == NULL) {
    return 0;
  }
  shader_parse_config(settings, text);
  free(text);
  snprintf(settings->preset, sizeof settings->preset, "%s", name);
  return 1;
}

int shader_param_steps(const struct shader_param *param)
{
  float range = param->max - param->min;

  if (param->step <= 0 || range <= 0) {
    return 1;
  }
  /* rounded, as the float range rarely divides exactly, and capped so the menu stays usable */
  range = range / param->step + 1.5f;
  return range > 10000 ? 10000 : (int)range;
}

int shader_param_index(const struct shader_param *param, float value)
{
  int steps = shader_param_steps(param);
  float at;

  if (steps <= 1) {
    return 0;
  }
  at = (value - param->min) / param->step + 0.5f;
  return at < 0 ? 0 : at >= steps ? steps - 1 : (int)at;
}

float shader_param_at(const struct shader_param *param, int index)
{
  float value = param->min + param->step * index;

  if (shader_param_steps(param) <= 1) {
    return param->def;
  }
  return value > param->max ? param->max : value;
}

float shader_param_value(const struct shader_settings *settings, const struct shader_param *param)
{
  float value = param->def;
  int i;

  for (i = 0; i < settings->value_count; i++) {
    if (strcmp(settings->values[i].name, param->name) == 0) {
      value = settings->values[i].value;
    }
  }
  if (param->max > param->min) {
    value = value < param->min ? param->min : value > param->max ? param->max : value;
  }
  return value;
}

#define PRAGMA "#pragma parameter"

int shader_parse_params(const char *source, struct shader_param *params, int max)
{
  char line[512];
  int count = 0;

  while (source != NULL && *source != '\0' && count < max) {
    size_t length = strcspn(source, "\n");
    struct shader_param *param = &params[count];

    snprintf(line, sizeof line, "%.*s", (int)(length < sizeof line ? length : sizeof line - 1),
             source);
    source += length + (source[length] == '\n');
    if (strncmp(line, PRAGMA, strlen(PRAGMA)) != 0) {
      continue;
    }
    if (sscanf(line + strlen(PRAGMA), " %127s \"%127[^\"]\" %f %f %f %f", param->name, param->label,
               &param->def, &param->min, &param->max, &param->step) == 6) {
      count++;
    }
  }

  return count;
}

int shader_load_params(const struct shader_settings *settings, const char *glsl_dir,
                       struct shader_param *params, int max)
{
  struct shader_param found[SHADER_MAX_PARAMS];
  char path[4096];
  int count = 0, pass, i, j;

  for (pass = 0; pass < settings->passes && pass < SHADER_PASSES; pass++) {
    char *source;
    int got;

    snprintf(path, sizeof path, "%s/%s", glsl_dir != NULL ? glsl_dir : "",
             settings->pass[pass].program);
    source = shader_read_file(path);
    got = shader_parse_params(source, found, SHADER_MAX_PARAMS);
    free(source);

    for (i = 0; i < got && count < max; i++) {
      for (j = 0; j < count && strcmp(params[j].name, found[i].name) != 0; j++) {
      }
      if (j == count) {
        params[count++] = found[i];
      }
    }
  }

  return count;
}

/* whether the #version line from start to end is a desktop one, which minarch moves to GLSL ES */
static int desktop_version(const char *start, const char *end)
{
  static const char *const versions[] = {"110", "120", "130", "140", "150", "330",
                                         "400", "410", "420", "430", "440", "450"};
  char line[32];
  size_t i;

  snprintf(line, sizeof line, "%.*s", (int)(end - start), start);
  for (i = 0; i < sizeof versions / sizeof *versions; i++) {
    char wanted[32];

    snprintf(wanted, sizeof wanted, "#version %s", versions[i]);
    if (strstr(line, wanted) != NULL) {
      return 1;
    }
  }
  return 0;
}

char *shader_wrap_source(const char *source, enum shader_stage stage, int es3)
{
  static const char precision[] = "#ifdef GL_ES\n"
                                  "#ifdef GL_OES_standard_derivatives\n"
                                  "#extension GL_OES_standard_derivatives : enable\n"
                                  "#endif\n"
                                  "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
                                  "precision highp float;\n"
                                  "#else\n"
                                  "precision mediump float;\n"
                                  "#endif\n"
                                  "#endif\n"
                                  "#define PARAMETER_UNIFORM\n";
  const char *define = stage == SHADER_VERTEX ? "#define VERTEX\n" : "#define FRAGMENT\n";
  const char *extra = stage == SHADER_FRAGMENT ? precision : "";
  size_t size = strlen(source) + strlen(define) + strlen(extra) + 32;
  char *cleaned = malloc(strlen(source) + 2), *out = malloc(size);
  const char *at = source, *version, *version_end;
  size_t used = 0, header;

  if (cleaned == NULL || out == NULL) {
    free(cleaned);
    free(out);
    return NULL;
  }

  /* every line but the parameters, each ending in a newline */
  while (*at != '\0') {
    size_t length = strcspn(at, "\n");

    if (strncmp(at, PRAGMA, strlen(PRAGMA)) != 0) {
      memcpy(cleaned + used, at, length);
      used += length;
      cleaned[used++] = '\n';
    }
    at += length + (at[length] == '\n');
  }
  cleaned[used] = '\0';

  /* a #version anywhere but the start of a line is in a comment */
  for (version = strstr(cleaned, "#version"); version != NULL && version != cleaned &&
                                              version[-1] != '\n';
       version = strstr(version + 1, "#version")) {
  }
  version_end = version != NULL ? strchr(version, '\n') : NULL;
  if (version_end != NULL && (size_t)(version_end - version) < 32 &&
      desktop_version(version, version_end)) {
    header = version_end - cleaned + 1;
    snprintf(out, size, "%s%s%s%s", es3 ? "#version 300 es\n" : "#version 100\n", define, extra,
             cleaned + header);
  } else if (version_end != NULL) {
    header = version_end - cleaned + 1;
    snprintf(out, size, "%.*s%s%s%s", (int)header, cleaned, define, extra, cleaned + header);
  } else {
    snprintf(out, size, "#version 100\n%s%s%s", define, extra, cleaned);
  }

  free(cleaned);
  return out;
}

static int clamp_size(int size, int max_size)
{
  return size < 1 ? 1 : max_size > 0 && size > max_size ? max_size : size;
}

void shader_pass_sizes(const struct shader_settings *settings, int orig_w, int orig_h, int screen_w,
                       int screen_h, int max_size, struct shader_pass_size *sizes)
{
  int last_w = orig_w, last_h = orig_h, i;

  for (i = 0; i < settings->passes && i < SHADER_PASSES; i++) {
    const struct shader_pass *pass = &settings->pass[i];
    struct shader_pass_size *size = &sizes[i];

    if (pass->upscale == SHADER_UPSCALE_SCREEN) {
      size->output_w = screen_w;
      size->output_h = screen_h;
    } else {
      size->output_w = last_w * (pass->upscale + 1);
      size->output_h = last_h * (pass->upscale + 1);
    }
    size->output_w = clamp_size(size->output_w, max_size);
    size->output_h = clamp_size(size->output_h, max_size);
    size->input_w = pass->srctype == SHADER_SOURCE ? orig_w : last_w;
    size->input_h = pass->srctype == SHADER_SOURCE ? orig_h : last_h;
    size->texture_w = pass->scaletype == SHADER_SOURCE ? orig_w : last_w;
    size->texture_h = pass->scaletype == SHADER_SOURCE ? orig_h : last_h;
    last_w = size->output_w;
    last_h = size->output_h;
  }
}

static int compare_names(const void *a, const void *b)
{
  return strcmp(*(char *const *)a, *(char *const *)b);
}

int shader_list_files(const char *dir, const char *suffix, char ***names)
{
  DIR *folder = dir != NULL && *dir != '\0' ? opendir(dir) : NULL;
  size_t suffix_length = strlen(suffix);
  struct dirent *entry;
  int count = 0;

  *names = NULL;
  if (folder == NULL) {
    return 0;
  }

  while ((entry = readdir(folder)) != NULL) {
    const char *name = entry->d_name;
    size_t length = strlen(name);
    char **grown;

    if (name[0] == '.' || length <= suffix_length ||
        strcmp(name + length - suffix_length, suffix) != 0) {
      continue;
    }
    grown = realloc(*names, (count + 1) * sizeof **names);
    if (grown == NULL) {
      break;
    }
    *names = grown;
    (*names)[count] = strdup(name);
    if ((*names)[count] != NULL) {
      count++;
    }
  }

  closedir(folder);
  qsort(*names, count, sizeof **names, compare_names);
  return count;
}
