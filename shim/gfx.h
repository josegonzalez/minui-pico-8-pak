/*
 * What the in-game menu is drawn with: a canvas of 0xAARRGGBB pixels, the
 * pills and rounded shapes minarch builds its menu from, text in the
 * launcher's own font through stb_truetype, and minarch's layout units.
 *
 * Nothing here calls SDL, so shim/test/menu-test.c exercises all of it without
 * pico-8 or a display. sdl-display.c uploads the finished canvas as a texture.
 */

#ifndef GFX_H
#define GFX_H

#include <stddef.h>
#include <stdint.h>

/* the pixel order SDL_PIXELFORMAT_ARGB8888 keeps in a uint32_t */
#define GFX_ARGB(rgb) (0xff000000u | (uint32_t)(rgb))

/* minarch's palette, from MinUI's common/defines.h */
#define GFX_WHITE GFX_ARGB(0xffffff)
#define GFX_BLACK GFX_ARGB(0x000000)
#define GFX_GRAY GFX_ARGB(0x999999)
#define GFX_LIGHT_GRAY GFX_ARGB(0x7f7f7f)
#define GFX_DARK_GRAY GFX_ARGB(0x262626)
#define GFX_RED GFX_ARGB(0xff3300)

/* minarch's layout, in units a gfx_metrics scales to the screen */
#define GFX_PILL_SIZE 30
#define GFX_BUTTON_SIZE 20
#define GFX_BUTTON_MARGIN 5
#define GFX_BUTTON_PADDING 12
#define GFX_OPTION_PADDING 8
#define GFX_SCROLL_HEIGHT 4
#define GFX_SETTINGS_SIZE 4
#define GFX_SETTINGS_WIDTH 80
#define GFX_FONT_LARGE 16
#define GFX_FONT_MEDIUM 14
#define GFX_FONT_SMALL 12
#define GFX_FONT_TINY 10

typedef struct {
  uint32_t *pixels;
  int w, h;
} gfx_canvas;

/*
 * Logs a line from the shim, stamped with the seconds since boot as
 * launch.sh's log_step stamps its own, so the two line up in the pak's log.
 */
void gfx_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

/* straight alpha source-over */
uint32_t gfx_blend(uint32_t dst, uint32_t src);
/* blends colour at one pixel, its alpha scaled by cover (0-255) */
void gfx_plot(gfx_canvas *canvas, int x, int y, uint32_t colour, int cover);
void gfx_fill(gfx_canvas *canvas, int x, int y, int w, int h, uint32_t colour);
/* opaque, with no blending */
void gfx_clear(gfx_canvas *canvas, uint32_t colour);
/* anti-aliased, and a radius of half the height makes a pill */
void gfx_rounded(gfx_canvas *canvas, int x, int y, int w, int h, int radius, uint32_t colour);
void gfx_pill(gfx_canvas *canvas, int x, int y, int w, int h, uint32_t colour);
void gfx_circle(gfx_canvas *canvas, int cx, int cy, int r, uint32_t colour);
/* every pixel whose centre lies inside the three points */
void gfx_triangle(gfx_canvas *canvas, int x0, int y0, int x1, int y1, int x2, int y2,
                  uint32_t colour);
/* part of a ring opening to the right of its centre, anti-aliased, for sound waves */
void gfx_arc(gfx_canvas *canvas, int cx, int cy, int r, int thickness, uint32_t colour);
/* an isosceles triangle in the box, pointing up or down, for scroll arrows */
void gfx_arrow(gfx_canvas *canvas, int x, int y, int w, int h, int down, uint32_t colour);
/* copies frame onto canvas darkened by alpha (0-255), as minarch dims the game */
void gfx_dim(gfx_canvas *canvas, const uint32_t *frame, int alpha);
/* draws a picture of src_w by src_h stretched to w by h at x, y, a pixel of it to each */
void gfx_blit_scaled(gfx_canvas *canvas, int x, int y, int w, int h, const uint32_t *src, int src_w,
                     int src_h);

typedef struct gfx_font gfx_font;

/*
 * A TrueType or CFF font, sized as SDL_ttf sizes them so a size 16 here is
 * minarch's large font at the same scale. Without a path, or with a file that
 * will not parse, it is a built-in 5x7 bitmap font instead, so this never
 * returns NULL unless memory runs out.
 */
gfx_font *gfx_font_open(const char *path);
void gfx_font_close(gfx_font *font);
int gfx_font_is_builtin(const gfx_font *font);
int gfx_text_width(const gfx_font *font, int px, const char *text);
/* the height of a line of text, ascent to descent */
int gfx_line_height(const gfx_font *font, int px);
/* draws with the top of the line at y and returns the x past the text */
int gfx_text_draw(gfx_canvas *canvas, const gfx_font *font, int x, int y, int px, uint32_t colour,
                  const char *text);
/* copies text into out, cut to max_w with "..." when it does not fit */
void gfx_text_fit(const gfx_font *font, int px, const char *text, int max_w, char *out,
                  size_t size);

/*
 * The look of the launcher pico-8 was started from. MinUI's is fixed; NextUI's
 * comes from the seven colours in its minuisettings.txt, whose roles are 1
 * main, 2 accent, 3 accent 2, 4 list text, 5 selected list text, 6 hint and
 * 7 background.
 */
typedef struct {
  int nextui;
  uint32_t color[7];
} gfx_theme;

/*
 * style is "nextui" or anything else for MinUI, and colors is NextUI's seven
 * colours joined with commas, each 0xRRGGBBAA, 0xRRGGBB or empty to keep the
 * default.
 */
void gfx_theme_init(gfx_theme *theme, const char *style, const char *colors);
int gfx_parse_colour(const char *text, uint32_t *out);
uint32_t gfx_title_pill(const gfx_theme *theme);
uint32_t gfx_title_text(const gfx_theme *theme);
uint32_t gfx_selected_pill(const gfx_theme *theme);
uint32_t gfx_selected_text(const gfx_theme *theme);
uint32_t gfx_text(const gfx_theme *theme);
uint32_t gfx_group_pill(const gfx_theme *theme);
uint32_t gfx_hint_text(const gfx_theme *theme);
uint32_t gfx_option_row(const gfx_theme *theme);
uint32_t gfx_background(const gfx_theme *theme);
/* the volume and brightness indicator's pill, icon, bar and the bar's track */
uint32_t gfx_indicator_pill(const gfx_theme *theme);
uint32_t gfx_indicator_icon(const gfx_theme *theme);
uint32_t gfx_indicator_bar(const gfx_theme *theme);
uint32_t gfx_indicator_track(const gfx_theme *theme);
/* NextUI's toast, whichever launcher started pico-8 */
uint32_t gfx_toast_pill(const gfx_theme *theme);
uint32_t gfx_toast_text(const gfx_theme *theme);
/* minarch draws a black copy under every unselected row, NextUI does not */
int gfx_shadow(const gfx_theme *theme);

typedef struct {
  /* -1 when there is no battery */
  int percent;
  int charging;
} gfx_battery;

/* reads whichever supply under root says its type is Battery */
gfx_battery gfx_read_battery(const char *root);

/*
 * minarch's units times a scale for the screen: 3 with a padding of 5 on the
 * 1024x768 Brick, 2 with a padding of 10 on everything at least 480 lines tall,
 * and a fraction of a unit below that, which no supported device is.
 */
typedef struct {
  double k;
  int padding;
} gfx_metrics;

gfx_metrics gfx_metrics_for(int w, int h);
int gfx_u(const gfx_metrics *metrics, double units);

#endif
