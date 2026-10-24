/*
 * Drawing for the in-game menu. See gfx.h.
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"

/*
 * Only the rasteriser is compiled, static so none of it is exported into
 * pico-8. stb_truetype reaches for libm, whose symbols carry glibc versions
 * newer than these devices have, so the few it uses to draw are done here.
 * pow, cos and acos are only used by its signed distance fields, which the menu
 * never asks for, and shim/Dockerfile checks the object imports none of them.
 */
static double gfx_floor(double x)
{
  double i = (double)(long long)x;

  return x < i ? i - 1 : i;
}

static double gfx_ceil(double x)
{
  double i = (double)(long long)x;

  return x > i ? i + 1 : i;
}

static double gfx_sqrt(double x)
{
  double root, next;
  int i;

  if (x <= 0) {
    return 0;
  }

  /* Newton's method from above only ever comes down, so it stops when it cannot */
  root = x > 1 ? x : 1;
  for (i = 0; i < 64; i++) {
    next = (root + x / root) / 2;
    if (next >= root) {
      break;
    }
    root = next;
  }

  return root;
}

static double gfx_fabs(double x)
{
  return x < 0 ? -x : x;
}

static double gfx_fmod(double x, double y)
{
  return y == 0 ? 0 : x - (double)(long long)(x / y) * y;
}

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_ifloor(x) ((int)gfx_floor(x))
#define STBTT_iceil(x) ((int)gfx_ceil(x))
#define STBTT_sqrt(x) gfx_sqrt(x)
#define STBTT_fabs(x) gfx_fabs(x)
#define STBTT_fmod(x, y) gfx_fmod(x, y)
#define STBTT_pow(x, y) __builtin_pow(x, y)
#define STBTT_cos(x) __builtin_cos(x)
#define STBTT_acos(x) __builtin_acos(x)
/* a font that trips one is drawn wrong rather than taking pico-8 down with it */
#define STBTT_assert(x) ((void)0)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "stb_truetype.h"
#pragma GCC diagnostic pop

uint32_t gfx_blend(uint32_t dst, uint32_t src)
{
  uint32_t src_a = src >> 24;
  uint32_t dst_weight, out_a, out = 0;
  int shift;

  if (src_a == 0xff) {
    return src;
  }
  if (src_a == 0) {
    return dst;
  }

  /* every term is out of 255 squared, so each channel divides once at the end */
  dst_weight = (dst >> 24) * (255 - src_a);
  out_a = src_a * 255 + dst_weight;
  for (shift = 0; shift <= 16; shift += 8) {
    uint32_t channel = (((src >> shift) & 0xff) * src_a * 255 +
                        ((dst >> shift) & 0xff) * dst_weight + out_a / 2) /
                       out_a;
    out |= (channel > 255 ? 255 : channel) << shift;
  }

  return out | ((out_a + 127) / 255) << 24;
}

void gfx_plot(gfx_canvas *canvas, int x, int y, uint32_t colour, int cover)
{
  uint32_t alpha;
  uint32_t *pixel;

  if (x < 0 || y < 0 || x >= canvas->w || y >= canvas->h || cover <= 0) {
    return;
  }

  alpha = ((colour >> 24) * (uint32_t)(cover > 255 ? 255 : cover) + 127) / 255;
  pixel = &canvas->pixels[(size_t)y * canvas->w + x];
  *pixel = gfx_blend(*pixel, (colour & 0x00ffffff) | alpha << 24);
}

void gfx_fill(gfx_canvas *canvas, int x, int y, int w, int h, uint32_t colour)
{
  int x0 = x < 0 ? 0 : x;
  int y0 = y < 0 ? 0 : y;
  int x1 = x + w > canvas->w ? canvas->w : x + w;
  int y1 = y + h > canvas->h ? canvas->h : y + h;
  int row, col;

  for (row = y0; row < y1; row++) {
    uint32_t *line = &canvas->pixels[(size_t)row * canvas->w];
    for (col = x0; col < x1; col++) {
      line[col] = gfx_blend(line[col], colour);
    }
  }
}

void gfx_clear(gfx_canvas *canvas, uint32_t colour)
{
  size_t i, count = (size_t)canvas->w * canvas->h;

  for (i = 0; i < count; i++) {
    canvas->pixels[i] = colour;
  }
}

/*
 * The straight bands are filled and each corner pixel is shaded by how far its
 * centre lies inside the corner's circle, which is what keeps a pill smooth.
 */
void gfx_rounded(gfx_canvas *canvas, int x, int y, int w, int h, int radius, uint32_t colour)
{
  int r = radius, dx, dy;

  if (w <= 0 || h <= 0) {
    return;
  }
  if (r > w / 2) {
    r = w / 2;
  }
  if (r > h / 2) {
    r = h / 2;
  }
  if (r <= 0) {
    gfx_fill(canvas, x, y, w, h, colour);
    return;
  }

  gfx_fill(canvas, x, y + r, w, h - 2 * r, colour);
  gfx_fill(canvas, x + r, y, w - 2 * r, r, colour);
  gfx_fill(canvas, x + r, y + h - r, w - 2 * r, r, colour);

  for (dy = 0; dy < r; dy++) {
    for (dx = 0; dx < r; dx++) {
      double fx = r - (dx + 0.5), fy = r - (dy + 0.5);
      double cover = r + 0.5 - gfx_sqrt(fx * fx + fy * fy);
      int level;

      if (cover <= 0) {
        continue;
      }
      level = cover >= 1 ? 255 : (int)(cover * 255 + 0.5);
      gfx_plot(canvas, x + dx, y + dy, colour, level);
      gfx_plot(canvas, x + w - 1 - dx, y + dy, colour, level);
      gfx_plot(canvas, x + dx, y + h - 1 - dy, colour, level);
      gfx_plot(canvas, x + w - 1 - dx, y + h - 1 - dy, colour, level);
    }
  }
}

void gfx_pill(gfx_canvas *canvas, int x, int y, int w, int h, uint32_t colour)
{
  gfx_rounded(canvas, x, y, w, h, h / 2, colour);
}

void gfx_circle(gfx_canvas *canvas, int cx, int cy, int r, uint32_t colour)
{
  gfx_rounded(canvas, cx - r, cy - r, 2 * r, 2 * r, r, colour);
}

/* which side of the line from a to b the point p lies on, doubled so centres stay whole */
static long edge(int ax, int ay, int bx, int by, int px, int py)
{
  return (long)(bx - ax) * (py - ay) - (long)(by - ay) * (px - ax);
}

void gfx_triangle(gfx_canvas *canvas, int x0, int y0, int x1, int y1, int x2, int y2,
                  uint32_t colour)
{
  int left = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
  int right = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
  int top = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
  int bottom = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
  long area = edge(x0, y0, x1, y1, x2, y2);
  int x, y;

  if (area == 0) {
    return;
  }

  for (y = top; y < bottom; y++) {
    for (x = left; x < right; x++) {
      /* pixel centres, at the doubled scale the corners are moved to */
      long a = edge(2 * x0, 2 * y0, 2 * x1, 2 * y1, 2 * x + 1, 2 * y + 1);
      long b = edge(2 * x1, 2 * y1, 2 * x2, 2 * y2, 2 * x + 1, 2 * y + 1);
      long c = edge(2 * x2, 2 * y2, 2 * x0, 2 * y0, 2 * x + 1, 2 * y + 1);

      if ((area > 0 && a >= 0 && b >= 0 && c >= 0) || (area < 0 && a <= 0 && b <= 0 && c <= 0)) {
        gfx_plot(canvas, x, y, colour, 255);
      }
    }
  }
}

void gfx_arrow(gfx_canvas *canvas, int x, int y, int w, int h, int down, uint32_t colour)
{
  int row;

  for (row = 0; row < h; row++) {
    /* the row's distance from the point sets its width */
    int from_tip = down ? h - 1 - row : row;
    int span = h > 1 ? w * (from_tip + 1) / h : w;
    gfx_fill(canvas, x + (w - span) / 2, y + row, span, 1, colour);
  }
}

void gfx_dim(gfx_canvas *canvas, const uint32_t *frame, int alpha)
{
  size_t i, count = (size_t)canvas->w * canvas->h;
  uint32_t keep = (uint32_t)(255 - alpha);

  for (i = 0; i < count; i++) {
    uint32_t c = frame[i];
    canvas->pixels[i] = 0xff000000u | (((c >> 16) & 0xff) * keep / 255) << 16 |
                        (((c >> 8) & 0xff) * keep / 255) << 8 | (c & 0xff) * keep / 255;
  }
}

/* ASCII 32 to 126, five columns each with the top row in the lowest bit */
static const unsigned char builtin_glyphs[95][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x5f, 0x00, 0x00}, {0x00, 0x07, 0x00, 0x07, 0x00},
    {0x14, 0x7f, 0x14, 0x7f, 0x14}, {0x24, 0x2a, 0x7f, 0x2a, 0x12}, {0x23, 0x13, 0x08, 0x64, 0x62},
    {0x36, 0x49, 0x55, 0x22, 0x50}, {0x00, 0x05, 0x03, 0x00, 0x00}, {0x00, 0x1c, 0x22, 0x41, 0x00},
    {0x00, 0x41, 0x22, 0x1c, 0x00}, {0x08, 0x2a, 0x1c, 0x2a, 0x08}, {0x08, 0x08, 0x3e, 0x08, 0x08},
    {0x00, 0x50, 0x30, 0x00, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08}, {0x00, 0x60, 0x60, 0x00, 0x00},
    {0x20, 0x10, 0x08, 0x04, 0x02}, {0x3e, 0x51, 0x49, 0x45, 0x3e}, {0x00, 0x42, 0x7f, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4b, 0x31}, {0x18, 0x14, 0x12, 0x7f, 0x10},
    {0x27, 0x45, 0x45, 0x45, 0x39}, {0x3c, 0x4a, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1e}, {0x00, 0x36, 0x36, 0x00, 0x00},
    {0x00, 0x56, 0x36, 0x00, 0x00}, {0x08, 0x14, 0x22, 0x41, 0x00}, {0x14, 0x14, 0x14, 0x14, 0x14},
    {0x00, 0x41, 0x22, 0x14, 0x08}, {0x02, 0x01, 0x51, 0x09, 0x06}, {0x32, 0x49, 0x79, 0x41, 0x3e},
    {0x7e, 0x11, 0x11, 0x11, 0x7e}, {0x7f, 0x49, 0x49, 0x49, 0x36}, {0x3e, 0x41, 0x41, 0x41, 0x22},
    {0x7f, 0x41, 0x41, 0x22, 0x1c}, {0x7f, 0x49, 0x49, 0x49, 0x41}, {0x7f, 0x09, 0x09, 0x09, 0x01},
    {0x3e, 0x41, 0x49, 0x49, 0x7a}, {0x7f, 0x08, 0x08, 0x08, 0x7f}, {0x00, 0x41, 0x7f, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3f, 0x01}, {0x7f, 0x08, 0x14, 0x22, 0x41}, {0x7f, 0x40, 0x40, 0x40, 0x40},
    {0x7f, 0x02, 0x0c, 0x02, 0x7f}, {0x7f, 0x04, 0x08, 0x10, 0x7f}, {0x3e, 0x41, 0x41, 0x41, 0x3e},
    {0x7f, 0x09, 0x09, 0x09, 0x06}, {0x3e, 0x41, 0x51, 0x21, 0x5e}, {0x7f, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7f, 0x01, 0x01}, {0x3f, 0x40, 0x40, 0x40, 0x3f},
    {0x1f, 0x20, 0x40, 0x20, 0x1f}, {0x3f, 0x40, 0x38, 0x40, 0x3f}, {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}, {0x00, 0x7f, 0x41, 0x41, 0x00},
    {0x02, 0x04, 0x08, 0x10, 0x20}, {0x00, 0x41, 0x41, 0x7f, 0x00}, {0x04, 0x02, 0x01, 0x02, 0x04},
    {0x40, 0x40, 0x40, 0x40, 0x40}, {0x00, 0x01, 0x02, 0x04, 0x00}, {0x20, 0x54, 0x54, 0x54, 0x78},
    {0x7f, 0x48, 0x44, 0x44, 0x38}, {0x38, 0x44, 0x44, 0x44, 0x20}, {0x38, 0x44, 0x44, 0x48, 0x7f},
    {0x38, 0x54, 0x54, 0x54, 0x18}, {0x08, 0x7e, 0x09, 0x01, 0x02}, {0x0c, 0x52, 0x52, 0x52, 0x3e},
    {0x7f, 0x08, 0x04, 0x04, 0x78}, {0x00, 0x44, 0x7d, 0x40, 0x00}, {0x20, 0x40, 0x44, 0x3d, 0x00},
    {0x7f, 0x10, 0x28, 0x44, 0x00}, {0x00, 0x41, 0x7f, 0x40, 0x00}, {0x7c, 0x04, 0x18, 0x04, 0x78},
    {0x7c, 0x08, 0x04, 0x04, 0x78}, {0x38, 0x44, 0x44, 0x44, 0x38}, {0x7c, 0x14, 0x14, 0x14, 0x08},
    {0x08, 0x14, 0x14, 0x18, 0x7c}, {0x7c, 0x08, 0x04, 0x04, 0x08}, {0x48, 0x54, 0x54, 0x54, 0x20},
    {0x04, 0x3f, 0x44, 0x40, 0x20}, {0x3c, 0x40, 0x40, 0x20, 0x7c}, {0x1c, 0x20, 0x40, 0x20, 0x1c},
    {0x3c, 0x40, 0x30, 0x40, 0x3c}, {0x44, 0x28, 0x10, 0x28, 0x44}, {0x0c, 0x50, 0x50, 0x50, 0x3c},
    {0x44, 0x64, 0x54, 0x4c, 0x44}, {0x00, 0x08, 0x36, 0x41, 0x00}, {0x00, 0x00, 0x7f, 0x00, 0x00},
    {0x00, 0x41, 0x36, 0x08, 0x00}, {0x08, 0x04, 0x08, 0x10, 0x08},
};

struct gfx_font {
  unsigned char *data;
  stbtt_fontinfo info;
  int ascent, descent;
};

gfx_font *gfx_font_open(const char *path)
{
  gfx_font *font = calloc(1, sizeof *font);
  FILE *file;
  long size;
  int offset;

  if (font == NULL || path == NULL || *path == '\0') {
    return font;
  }

  file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "sdl-display: cannot open the font %s, using the built-in one\n", path);
    return font;
  }

  if (fseek(file, 0, SEEK_END) == 0 && (size = ftell(file)) > 0 && fseek(file, 0, SEEK_SET) == 0) {
    font->data = malloc((size_t)size);
    if (font->data != NULL && fread(font->data, 1, (size_t)size, file) != (size_t)size) {
      free(font->data);
      font->data = NULL;
    }
  }
  fclose(file);

  offset = font->data != NULL ? stbtt_GetFontOffsetForIndex(font->data, 0) : -1;
  if (offset < 0 || !stbtt_InitFont(&font->info, font->data, offset)) {
    fprintf(stderr, "sdl-display: cannot read the font %s, using the built-in one\n", path);
    free(font->data);
    font->data = NULL;
    return font;
  }

  stbtt_GetFontVMetrics(&font->info, &font->ascent, &font->descent, NULL);
  return font;
}

void gfx_font_close(gfx_font *font)
{
  if (font != NULL) {
    free(font->data);
    free(font);
  }
}

int gfx_font_is_builtin(const gfx_font *font)
{
  return font->data == NULL;
}

/* the built-in font's pixel size for a line px tall */
static int builtin_scale(int px)
{
  return px / 8 > 1 ? px / 8 : 1;
}

/* the next code point, with a malformed sequence read as one U+FFFD */
static uint32_t next_codepoint(const char **text)
{
  const unsigned char *at = (const unsigned char *)*text;
  int length = at[0] >= 0xf0 ? 4 : at[0] >= 0xe0 ? 3 : at[0] >= 0xc0 ? 2 : at[0] >= 0x80 ? 0 : 1;
  uint32_t codepoint;
  int i;

  if (length == 0) {
    *text += 1;
    return 0xfffd;
  }

  codepoint = length == 1 ? at[0] : at[0] & (0x7f >> length);
  for (i = 1; i < length; i++) {
    if ((at[i] & 0xc0) != 0x80) {
      *text += i;
      return 0xfffd;
    }
    codepoint = codepoint << 6 | (at[i] & 0x3f);
  }

  *text += length;
  return codepoint;
}

static int glyph_index(const gfx_font *font, uint32_t codepoint)
{
  int index = stbtt_FindGlyphIndex(&font->info, (int)codepoint);

  if (index == 0 && codepoint != ' ') {
    index = stbtt_FindGlyphIndex(&font->info, '?');
  }

  return index;
}

static int round_int(float value)
{
  return value < 0 ? (int)(value - 0.5f) : (int)(value + 0.5f);
}

int gfx_text_width(const gfx_font *font, int px, const char *text)
{
  float scale;
  int width = 0, advance, bearing;

  if (gfx_font_is_builtin(font)) {
    while (*text != '\0') {
      next_codepoint(&text);
      width += 6 * builtin_scale(px);
    }
    /* the last column's gap is not part of the text */
    return width > 0 ? width - builtin_scale(px) : 0;
  }

  scale = stbtt_ScaleForMappingEmToPixels(&font->info, (float)px);
  while (*text != '\0') {
    stbtt_GetGlyphHMetrics(&font->info, glyph_index(font, next_codepoint(&text)), &advance,
                           &bearing);
    width += round_int(advance * scale);
  }

  return width;
}

int gfx_line_height(const gfx_font *font, int px)
{
  float scale;

  if (gfx_font_is_builtin(font)) {
    return px + px / 4;
  }

  scale = stbtt_ScaleForMappingEmToPixels(&font->info, (float)px);
  return round_int(font->ascent * scale) - round_int(font->descent * scale);
}

static int draw_builtin(gfx_canvas *canvas, const gfx_font *font, int x, int y, int px,
                        uint32_t colour, const char *text)
{
  int scale = builtin_scale(px);
  int top = y + (gfx_line_height(font, px) - 7 * scale) / 2;
  int col, row;

  while (*text != '\0') {
    uint32_t codepoint = next_codepoint(&text);
    const unsigned char *glyph =
        builtin_glyphs[codepoint >= 32 && codepoint <= 126 ? codepoint - 32 : '?' - 32];

    for (col = 0; col < 5; col++) {
      for (row = 0; row < 7; row++) {
        if (glyph[col] & 1 << row) {
          gfx_fill(canvas, x + col * scale, top + row * scale, scale, scale, colour);
        }
      }
    }
    x += 6 * scale;
  }

  return x;
}

int gfx_text_draw(gfx_canvas *canvas, const gfx_font *font, int x, int y, int px, uint32_t colour,
                  const char *text)
{
  float scale;
  int baseline;

  if (gfx_font_is_builtin(font)) {
    return draw_builtin(canvas, font, x, y, px, colour, text);
  }

  scale = stbtt_ScaleForMappingEmToPixels(&font->info, (float)px);
  baseline = y + round_int(font->ascent * scale);
  while (*text != '\0') {
    int index = glyph_index(font, next_codepoint(&text));
    int advance, bearing, x0, y0, x1, y1, gx, gy;
    unsigned char *alpha;

    stbtt_GetGlyphHMetrics(&font->info, index, &advance, &bearing);
    stbtt_GetGlyphBitmapBox(&font->info, index, scale, scale, &x0, &y0, &x1, &y1);
    if (x1 > x0 && y1 > y0 && (alpha = malloc((size_t)(x1 - x0) * (y1 - y0))) != NULL) {
      stbtt_MakeGlyphBitmap(&font->info, alpha, x1 - x0, y1 - y0, x1 - x0, scale, scale, index);
      for (gy = 0; gy < y1 - y0; gy++) {
        for (gx = 0; gx < x1 - x0; gx++) {
          gfx_plot(canvas, x + x0 + gx, baseline + y0 + gy, colour, alpha[gy * (x1 - x0) + gx]);
        }
      }
      free(alpha);
    }
    x += round_int(advance * scale);
  }

  return x;
}

void gfx_text_fit(const gfx_font *font, int px, const char *text, int max_w, char *out,
                  size_t size)
{
  size_t length;

  if (size == 0) {
    return;
  }

  snprintf(out, size, "%s", text);
  if (gfx_text_width(font, px, out) <= max_w) {
    return;
  }

  /* drop whole code points, then make room for the ellipsis */
  length = strlen(out);
  while (length > 0) {
    do {
      length--;
    } while (length > 0 && ((unsigned char)out[length] & 0xc0) == 0x80);
    if (length + 4 > size) {
      continue;
    }
    memcpy(out + length, "...", 4);
    if (gfx_text_width(font, px, out) <= max_w) {
      return;
    }
  }

  snprintf(out, size, "%s", size > 3 && gfx_text_width(font, px, "...") <= max_w ? "..." : "");
}

static const uint32_t nextui_defaults[7] = {GFX_ARGB(0xffffff), GFX_ARGB(0x9b2257),
                                            GFX_ARGB(0x1e2329), GFX_ARGB(0xffffff),
                                            GFX_ARGB(0x000000), GFX_ARGB(0xffffff),
                                            GFX_ARGB(0x000000)};

int gfx_parse_colour(const char *text, uint32_t *out)
{
  char *end;
  size_t length;
  unsigned long value;
  size_t i;

  if (text == NULL) {
    return 0;
  }
  if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    text += 2;
  } else if (text[0] == '#') {
    text++;
  }

  length = strlen(text);
  if (length != 6 && length != 8) {
    return 0;
  }
  for (i = 0; i < length; i++) {
    char c = text[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
      return 0;
    }
  }

  value = strtoul(text, &end, 16);
  /* NextUI writes an alpha byte, but the menu's pills are always opaque */
  if (length == 8) {
    value >>= 8;
  }
  *out = GFX_ARGB(value & 0xffffff);
  return 1;
}

void gfx_theme_init(gfx_theme *theme, const char *style, const char *colors)
{
  char field[32];
  int i;

  theme->nextui = style != NULL && strcmp(style, "nextui") == 0;
  memcpy(theme->color, nextui_defaults, sizeof theme->color);

  for (i = 0; i < 7 && colors != NULL && *colors != '\0'; i++) {
    size_t length = strcspn(colors, ",");
    uint32_t parsed;

    if (length < sizeof field) {
      memcpy(field, colors, length);
      field[length] = '\0';
      if (gfx_parse_colour(field, &parsed)) {
        theme->color[i] = parsed;
      }
    }
    colors += length;
    if (*colors == ',') {
      colors++;
    }
  }
}

uint32_t gfx_title_pill(const gfx_theme *theme)
{
  return theme->nextui ? theme->color[1] : GFX_BLACK;
}

uint32_t gfx_title_text(const gfx_theme *theme)
{
  return theme->nextui ? theme->color[5] : GFX_WHITE;
}

uint32_t gfx_selected_pill(const gfx_theme *theme)
{
  return theme->nextui ? theme->color[0] : GFX_WHITE;
}

uint32_t gfx_selected_text(const gfx_theme *theme)
{
  return theme->nextui ? theme->color[4] : GFX_BLACK;
}

uint32_t gfx_text(const gfx_theme *theme)
{
  return theme->nextui ? theme->color[3] : GFX_WHITE;
}

uint32_t gfx_group_pill(const gfx_theme *theme)
{
  return theme->nextui ? theme->color[2] : GFX_BLACK;
}

uint32_t gfx_hint_text(const gfx_theme *theme)
{
  return theme->nextui ? theme->color[5] : GFX_WHITE;
}

uint32_t gfx_option_row(const gfx_theme *theme)
{
  return theme->nextui ? theme->color[1] : GFX_DARK_GRAY;
}

uint32_t gfx_background(const gfx_theme *theme)
{
  return theme->nextui ? theme->color[6] : GFX_BLACK;
}

int gfx_shadow(const gfx_theme *theme)
{
  return !theme->nextui;
}

static void read_first_line(const char *path, char *line, size_t size)
{
  FILE *file = fopen(path, "r");

  line[0] = '\0';
  if (file != NULL) {
    if (fgets(line, (int)size, file) == NULL) {
      line[0] = '\0';
    }
    fclose(file);
  }
  line[strcspn(line, "\r\n")] = '\0';
}

gfx_battery gfx_read_battery(const char *root)
{
  gfx_battery battery = {-1, 0};
  DIR *dir = opendir(root);
  struct dirent *entry;
  char path[4096], line[64];

  if (dir == NULL) {
    return battery;
  }

  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.') {
      continue;
    }

    snprintf(path, sizeof path, "%s/%s/type", root, entry->d_name);
    read_first_line(path, line, sizeof line);
    if (strcmp(line, "Battery") != 0) {
      continue;
    }

    snprintf(path, sizeof path, "%s/%s/capacity", root, entry->d_name);
    read_first_line(path, line, sizeof line);
    if (line[0] == '\0') {
      continue;
    }
    battery.percent = atoi(line);
    battery.percent = battery.percent < 0 ? 0 : battery.percent > 100 ? 100 : battery.percent;

    snprintf(path, sizeof path, "%s/%s/status", root, entry->d_name);
    read_first_line(path, line, sizeof line);
    battery.charging = strcmp(line, "Charging") == 0;
    break;
  }

  closedir(dir);
  return battery;
}

gfx_metrics gfx_metrics_for(int w, int h)
{
  gfx_metrics metrics = {2.0, 10};

  if (w == 1024 && h == 768) {
    metrics.k = 3.0;
    metrics.padding = 5;
  } else if (h < 480) {
    metrics.k = h / 320.0 > 0.5 ? h / 320.0 : 0.5;
  }

  return metrics;
}

int gfx_u(const gfx_metrics *metrics, double units)
{
  return (int)(units * metrics->k + 0.5);
}
