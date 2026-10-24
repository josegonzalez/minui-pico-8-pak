/*
 * What the display library knows about the cart pico-8 was started with and
 * the roms folders around it: the name the game list shows for it, the preview
 * NextUI's game switcher shows for it, and the covers of the Splore carts.
 *
 * launch.sh used to do all of this, before pico-8 started or after it exited,
 * where every step added to the wait. Here it is done when it is needed: the
 * title when the menu first opens, the preview once pico-8 has started and
 * again on Quit, and the Splore covers when the menu changes them.
 */

#ifndef CART_H
#define CART_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/*
 * The name the game list shows: the cart's row in map.txt, or its file name
 * without the extension and the "1) " MinUI and NextUI sort by and leave off.
 * A multi-cart game is listed as its folder, the m3u's.
 */
void cart_title(const char *cart_path, char *out, size_t size);

/* NextUI keys the switcher by the cart's file name, or by its m3u's */
void cart_switcher_name(const char *cart_path, char *out, size_t size);

/*
 * The picture that stands for the cart when there is no frame of the game:
 * its image in .media, or the cart itself, as most carts are PNG images
 * whatever they are named. Returns 0, with out empty, when there is neither.
 */
int cart_artwork(const char *cart_path, char *out, size_t size);

/*
 * NextUI's game switcher draws a game's preview, and offers to resume it, only
 * when <name>.txt in its folder names a slot and <name>.<slot>.bmp holds the
 * picture. These write slot 0 with the cart's artwork, or with a frame of the
 * game as a BMP a fraction of the screen's size, and return 0 when nothing
 * could be written.
 */
int cart_write_preview_artwork(const char *switcher_dir, const char *cart_path);
int cart_write_preview_frame(const char *switcher_dir, const char *cart_path,
                             const uint32_t *pixels, int w, int h);

/* how many of the screen's pixels each of the frame preview's stands for */
int cart_preview_step(int w, int h);

/*
 * Saves a screenshot to dir as NextUI saves its own, a PNG named for the game
 * and when it was taken, <name>.<YYYY-MM-DD-HH-MM-SS>.png, making dir if it is
 * missing. Puts the path it wrote in written, and returns 0 when it could not.
 */
int cart_write_screenshot(const char *dir, const char *cart_path, const uint32_t *pixels, int w,
                          int h, time_t when, char *written, size_t size);

/* makes path and any folder above it that is missing, as mkdir -p does, or returns 0 */
int cart_make_dirs(const char *path);

/*
 * Gives every Splore cart in the roms folders tagged "(<tag>)" the pak's
 * fancy or standard cover, under the names both launchers look for. Only
 * covers that are still one of the pak's two are changed, so artwork of the
 * user's own is left alone, as is a cart with none. Returns how many changed.
 */
int cart_apply_splore_artwork(const char *roms_dir, const char *tag, const char *covers_dir,
                              int fancy);

#endif
