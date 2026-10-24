/*
 * The cart and the roms folders around it. See cart.h.
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "cart.h"
#include "gfx.h"

/*
 * Only the PNG encoder is used, writing to memory, static so none of it is
 * exported into pico-8. Its HDR writer would take frexp from libm, but nothing
 * calls it, so it is never built in, which shim/Dockerfile checks. Its file
 * writers stay in, unused: STBI_WRITE_NO_STDIO leaves the HDR one declared
 * but not defined, which gcc refuses.
 */
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_ASSERT(x) ((void)0)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "stb_image_write.h"
#pragma GCC diagnostic pop

#define LOG(...) gfx_log(__VA_ARGS__)

static int is_file(const char *path)
{
  struct stat info;

  return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

static int is_dir(const char *path)
{
  struct stat info;

  return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

/* the part of path before its last slash, and the part after it */
static void split_path(const char *path, char *dir, size_t dir_size, char *base, size_t base_size)
{
  const char *slash = strrchr(path, '/');

  if (slash == NULL) {
    snprintf(dir, dir_size, ".");
    snprintf(base, base_size, "%s", path);
    return;
  }
  snprintf(dir, dir_size, "%.*s", (int)(slash - path), path);
  snprintf(base, base_size, "%s", slash + 1);
}

static int ends_with_nocase(const char *text, const char *suffix)
{
  size_t length = strlen(text), suffix_length = strlen(suffix);

  return length >= suffix_length && strcasecmp(text + length - suffix_length, suffix) == 0;
}

/*
 * Works out how the launchers see the cart: whether it is part of a
 * multi-cart game, which is listed as the folder holding a matching m3u, and
 * the folder and name that listing goes by.
 */
struct listing {
  char folder[4096];
  char name[1024];
  int multi_cart;
  /* the folder the cart is in, and its file name */
  char cart_folder[4096];
  char cart_file[1024];
};

static void list_cart(const char *cart_path, struct listing *listing)
{
  char m3u[8192], folder_name[1024], parent[4096];

  split_path(cart_path, listing->cart_folder, sizeof listing->cart_folder, listing->cart_file,
             sizeof listing->cart_file);
  split_path(listing->cart_folder, parent, sizeof parent, folder_name, sizeof folder_name);

  snprintf(m3u, sizeof m3u, "%s/%s.m3u", listing->cart_folder, folder_name);
  listing->multi_cart = is_file(m3u);
  if (listing->multi_cart) {
    snprintf(listing->folder, sizeof listing->folder, "%s", parent);
    snprintf(listing->name, sizeof listing->name, "%s", folder_name);
  } else {
    snprintf(listing->folder, sizeof listing->folder, "%s", listing->cart_folder);
    snprintf(listing->name, sizeof listing->name, "%s", listing->cart_file);
  }
}

/* the name map.txt gives key, a row of the key, a tab and the name */
static int read_map(const char *folder, const char *key, char *out, size_t size)
{
  char path[8192], line[2048];
  size_t key_length = strlen(key);
  FILE *file;
  int found = 0;

  snprintf(path, sizeof path, "%s/map.txt", folder);
  file = fopen(path, "r");
  if (file == NULL) {
    return 0;
  }

  while (!found && fgets(line, sizeof line, file) != NULL) {
    line[strcspn(line, "\r\n")] = '\0';
    if (strncmp(line, key, key_length) == 0 && line[key_length] == '\t' &&
        line[key_length + 1] != '\0') {
      snprintf(out, size, "%s", line + key_length + 1);
      found = 1;
    }
  }

  fclose(file);
  return found;
}

void cart_title(const char *cart_path, char *out, size_t size)
{
  struct listing listing;
  char name[1024];
  const char *rest;
  size_t length;

  list_cart(cart_path, &listing);
  if (read_map(listing.folder, listing.name, out, size)) {
    return;
  }

  snprintf(name, sizeof name, "%s", listing.name);
  if (!listing.multi_cart) {
    if (ends_with_nocase(name, ".png")) {
      name[strlen(name) - 4] = '\0';
    }
    if (ends_with_nocase(name, ".p8")) {
      name[strlen(name) - 3] = '\0';
    }
  }

  /* "1) " is dropped, but only when what comes before the bracket is a number */
  length = strspn(name, "0123456789");
  rest = name + length;
  if (length > 0 && rest[0] == ')' && rest[1] == ' ') {
    snprintf(out, size, "%s", rest + 2);
    return;
  }
  snprintf(out, size, "%s", name);
}

void cart_switcher_name(const char *cart_path, char *out, size_t size)
{
  struct listing listing;

  list_cart(cart_path, &listing);
  if (listing.multi_cart) {
    /* a name cut short would key a different game, so none is better */
    if (snprintf(out, size, "%s.m3u", listing.name) >= (int)size) {
      out[0] = '\0';
    }
  } else {
    snprintf(out, size, "%s", listing.cart_file);
  }
}

/* a PNG begins with 0x89 and then the letters PNG, whatever its name */
static int is_png(const char *path)
{
  unsigned char signature[4];
  FILE *file = fopen(path, "rb");
  int png;

  if (file == NULL) {
    return 0;
  }
  png = fread(signature, 1, sizeof signature, file) == sizeof signature &&
        memcmp(signature + 1, "PNG", 3) == 0;
  fclose(file);
  return png;
}

int cart_artwork(const char *cart_path, char *out, size_t size)
{
  struct listing listing;
  char switcher_name[1024], path[8192];
  char *dot;

  list_cart(cart_path, &listing);

  /* NextUI draws a multi-cart game's folder with the artwork from the folder above it */
  if (listing.multi_cart) {
    snprintf(path, sizeof path, "%s/.media/%s.png", listing.folder, listing.name);
    if (is_file(path)) {
      snprintf(out, size, "%s", path);
      return 1;
    }
  }

  /* and names artwork after the switcher's name, without its last extension */
  cart_switcher_name(cart_path, switcher_name, sizeof switcher_name);
  dot = strrchr(switcher_name, '.');
  if (dot != NULL) {
    *dot = '\0';
  }
  snprintf(path, sizeof path, "%s/.media/%s.png", listing.cart_folder, switcher_name);
  if (is_file(path)) {
    snprintf(out, size, "%s", path);
    return 1;
  }

  /* a cart saved as text would leave NextUI a picture it cannot load */
  if (is_png(cart_path)) {
    snprintf(out, size, "%s", cart_path);
    return 1;
  }

  out[0] = '\0';
  return 0;
}

static int copy_file(const char *from, const char *to)
{
  char buffer[65536];
  FILE *in = fopen(from, "rb"), *out;
  size_t got;
  int ok = 1;

  if (in == NULL) {
    return 0;
  }
  out = fopen(to, "wb");
  if (out == NULL) {
    fclose(in);
    return 0;
  }

  while ((got = fread(buffer, 1, sizeof buffer, in)) > 0) {
    if (fwrite(buffer, 1, got, out) != got) {
      ok = 0;
      break;
    }
  }
  ok = ok && !ferror(in);
  fclose(in);
  return fclose(out) == 0 && ok;
}

static int same_contents(const char *a, const char *b)
{
  char buffer_a[8192], buffer_b[8192];
  FILE *file_a = fopen(a, "rb"), *file_b = fopen(b, "rb");
  size_t got_a, got_b;
  int same = file_a != NULL && file_b != NULL;

  while (same) {
    got_a = fread(buffer_a, 1, sizeof buffer_a, file_a);
    got_b = fread(buffer_b, 1, sizeof buffer_b, file_b);
    if (got_a != got_b || memcmp(buffer_a, buffer_b, got_a) != 0) {
      same = 0;
    } else if (got_a == 0) {
      break;
    }
  }

  if (file_a != NULL) {
    fclose(file_a);
  }
  if (file_b != NULL) {
    fclose(file_b);
  }
  return same;
}

/* makes path and any folder above it that is missing, as mkdir -p does */
static int make_dirs(const char *path)
{
  char partial[4096];
  size_t i;

  if (snprintf(partial, sizeof partial, "%s", path) >= (int)sizeof partial) {
    return 0;
  }
  for (i = 1; partial[i] != '\0'; i++) {
    if (partial[i] == '/') {
      partial[i] = '\0';
      if (!is_dir(partial) && mkdir(partial, 0755) != 0) {
        return 0;
      }
      partial[i] = '/';
    }
  }
  return is_dir(partial) || mkdir(partial, 0755) == 0;
}

static int open_switcher(const char *switcher_dir, const char *cart_path, char *name,
                         size_t size)
{
  if (switcher_dir == NULL || *switcher_dir == '\0') {
    return 0;
  }
  if (!make_dirs(switcher_dir)) {
    LOG("cannot make %s, the game switcher will show no preview\n", switcher_dir);
    return 0;
  }

  cart_switcher_name(cart_path, name, size);
  return 1;
}

static int write_slot(const char *switcher_dir, const char *name)
{
  char path[8192];
  FILE *file;

  snprintf(path, sizeof path, "%s/%s.txt", switcher_dir, name);
  file = fopen(path, "w");
  if (file == NULL) {
    return 0;
  }
  fputs("0\n", file);
  return fclose(file) == 0;
}

int cart_write_preview_artwork(const char *switcher_dir, const char *cart_path)
{
  char name[1024], artwork[8192], path[8192];

  if (!open_switcher(switcher_dir, cart_path, name, sizeof name)) {
    return 0;
  }
  if (!cart_artwork(cart_path, artwork, sizeof artwork)) {
    LOG("no artwork for %s, the game switcher will show no preview\n", name);
    return 0;
  }

  /* NextUI loads the picture by its content, so a PNG under a .bmp name works */
  snprintf(path, sizeof path, "%s/%s.0.bmp", switcher_dir, name);
  if (!copy_file(artwork, path) || !write_slot(switcher_dir, name)) {
    LOG("cannot write the game switcher preview for %s to %s\n", name, switcher_dir);
    return 0;
  }

  LOG("gave the game switcher %s as the preview for %s\n", artwork, name);
  return 1;
}

static void put16(unsigned char *at, unsigned value)
{
  at[0] = value & 0xff;
  at[1] = (value >> 8) & 0xff;
}

static void put32(unsigned char *at, uint32_t value)
{
  put16(at, value & 0xffff);
  put16(at + 2, value >> 16);
}

/*
 * How much smaller than the screen the preview is: every step-th pixel of
 * every step-th row, the biggest step that leaves the picture at least
 * PREVIEW_MIN_HEIGHT tall. On the Brick, whose 768 lines show pico-8's 128 at
 * six times, that is a third, which keeps each of pico-8's pixels two wide.
 */
#define PREVIEW_MIN_HEIGHT 256

int cart_preview_step(int w, int h)
{
  int step = h / PREVIEW_MIN_HEIGHT;

  (void)w;
  return step > 1 ? step : 1;
}

/*
 * Writes data under a temporary name and then puts it in place. It is one
 * write, as the SD card makes each write wait for the card, and a file cut
 * short never replaces the one there.
 */
static int write_whole(const char *path, const void *data, size_t size)
{
  char temporary[8192];
  FILE *file;
  int ok;

  if (snprintf(temporary, sizeof temporary, "%s.part", path) >= (int)sizeof temporary) {
    return 0;
  }
  file = fopen(temporary, "wb");
  ok = file != NULL && fwrite(data, 1, size, file) == size;
  if (file != NULL) {
    ok = fclose(file) == 0 && ok;
  }
  if (!ok || rename(temporary, path) != 0) {
    unlink(temporary);
    return 0;
  }
  return 1;
}

/* a 24-bit BMP, which every SDL_image loads, of every step-th pixel */
static int write_bmp(const char *path, const uint32_t *pixels, int w, int h, int step)
{
  int out_w = w / step, out_h = h / step, x, y;
  size_t row_size = ((size_t)out_w * 3 + 3) & ~(size_t)3;
  size_t size = 54 + row_size * out_h;
  unsigned char *bmp = calloc(1, size);
  int ok;

  if (bmp == NULL || out_w <= 0 || out_h <= 0) {
    free(bmp);
    return 0;
  }

  bmp[0] = 'B';
  bmp[1] = 'M';
  put32(bmp + 2, (uint32_t)size);
  put32(bmp + 10, 54);
  put32(bmp + 14, 40);
  put32(bmp + 18, (uint32_t)out_w);
  put32(bmp + 22, (uint32_t)out_h);
  put16(bmp + 26, 1);
  put16(bmp + 28, 24);
  put32(bmp + 34, (uint32_t)(row_size * out_h));

  /* bottom row first, each pixel blue, green, red */
  for (y = 0; y < out_h; y++) {
    const uint32_t *source = pixels + (size_t)(out_h - 1 - y) * step * w;
    unsigned char *row = bmp + 54 + row_size * y;
    for (x = 0; x < out_w; x++) {
      uint32_t pixel = source[(size_t)x * step];
      row[x * 3] = pixel & 0xff;
      row[x * 3 + 1] = (pixel >> 8) & 0xff;
      row[x * 3 + 2] = (pixel >> 16) & 0xff;
    }
  }

  ok = write_whole(path, bmp, size);
  free(bmp);
  return ok;
}

int cart_write_preview_frame(const char *switcher_dir, const char *cart_path,
                             const uint32_t *pixels, int w, int h)
{
  char name[1024], path[8192];
  int step = cart_preview_step(w, h);

  if (!open_switcher(switcher_dir, cart_path, name, sizeof name)) {
    return 0;
  }

  snprintf(path, sizeof path, "%s/%s.0.bmp", switcher_dir, name);
  if (!write_bmp(path, pixels, w, h, step) || !write_slot(switcher_dir, name)) {
    LOG("cannot write the game switcher preview for %s to %s\n", name, switcher_dir);
    return 0;
  }

  LOG("gave the game switcher the last frame as the preview for %s, at %dx%d\n", name, w / step,
      h / step);
  return 1;
}

int cart_write_screenshot(const char *dir, const char *cart_path, const uint32_t *pixels, int w,
                          int h, time_t when, char *written, size_t size)
{
  char title[256], stamp[32], path[8192];
  unsigned char *rgb, *png;
  struct tm local;
  size_t i, count = (size_t)w * h;
  int png_size, ok;
  char *slash;

  if (dir == NULL || *dir == '\0' || cart_path == NULL || w <= 0 || h <= 0 || !make_dirs(dir)) {
    return 0;
  }

  /* NextUI's name: the game's name and when, as <name>.<YYYY-MM-DD-HH-MM-SS>.png */
  cart_title(cart_path, title, sizeof title);
  while ((slash = strchr(title, '/')) != NULL) {
    *slash = '-';
  }
  localtime_r(&when, &local);
  strftime(stamp, sizeof stamp, "%Y-%m-%d-%H-%M-%S", &local);
  if (snprintf(path, sizeof path, "%s/%s.%s.png", dir, title, stamp) >= (int)sizeof path) {
    return 0;
  }

  rgb = malloc(count * 3);
  if (rgb == NULL) {
    return 0;
  }
  for (i = 0; i < count; i++) {
    rgb[i * 3] = (pixels[i] >> 16) & 0xff;
    rgb[i * 3 + 1] = (pixels[i] >> 8) & 0xff;
    rgb[i * 3 + 2] = pixels[i] & 0xff;
  }

  /* a light compression, as this runs on the device's slow cores */
  stbi_write_png_compression_level = 2;
  png = stbi_write_png_to_mem(rgb, w * 3, w, h, 3, &png_size);
  free(rgb);
  if (png == NULL) {
    return 0;
  }

  ok = write_whole(path, png, (size_t)png_size);
  free(png);
  if (!ok) {
    LOG("cannot save the screenshot %s\n", path);
    return 0;
  }

  LOG("saved the screenshot %s\n", path);
  if (written != NULL) {
    snprintf(written, size, "%s", path);
  }
  return 1;
}

/* replaces cover with chosen when it is still one of the pak's two covers */
static int apply_cover(const char *cover, const char *chosen, const char *other)
{
  if (!is_file(cover) || same_contents(cover, chosen) || !same_contents(cover, other)) {
    return 0;
  }
  if (!copy_file(chosen, cover)) {
    LOG("cannot replace the Splore cover %s\n", cover);
    return 0;
  }
  LOG("gave %s the %s Splore cover\n", cover, strstr(chosen, "fancy") ? "fancy" : "standard");
  return 1;
}

int cart_apply_splore_artwork(const char *roms_dir, const char *tag, const char *covers_dir,
                              int fancy)
{
  char standard[8192], fancy_cover[8192], suffix[256], folder[8192], cover[16384];
  const char *chosen, *other;
  struct dirent *roms_entry, *entry;
  DIR *roms, *carts;
  int changed = 0;

  if (roms_dir == NULL || tag == NULL || covers_dir == NULL || *roms_dir == '\0') {
    return 0;
  }

  snprintf(standard, sizeof standard, "%s/Splore.p8.png", covers_dir);
  snprintf(fancy_cover, sizeof fancy_cover, "%s/Splore-fancy.p8.png", covers_dir);
  chosen = fancy ? fancy_cover : standard;
  other = fancy ? standard : fancy_cover;
  if (!is_file(chosen) || !is_file(other)) {
    LOG("the pak's Splore covers are missing from %s, leaving the covers alone\n", covers_dir);
    return 0;
  }

  roms = opendir(roms_dir);
  if (roms == NULL) {
    return 0;
  }
  snprintf(suffix, sizeof suffix, "(%s)", tag);

  while ((roms_entry = readdir(roms)) != NULL) {
    size_t length = strlen(roms_entry->d_name), suffix_length = strlen(suffix);

    if (length < suffix_length || strcmp(roms_entry->d_name + length - suffix_length, suffix) != 0) {
      continue;
    }
    snprintf(folder, sizeof folder, "%s/%s", roms_dir, roms_entry->d_name);
    carts = opendir(folder);
    if (carts == NULL) {
      continue;
    }

    /* every name with Splore in it, as launch.sh's is_splore_cart decides */
    while ((entry = readdir(carts)) != NULL) {
      char base[1024];
      char *dot;

      if (entry->d_name[0] == '.' ||
          (strstr(entry->d_name, "Splore") == NULL && strstr(entry->d_name, "splore") == NULL)) {
        continue;
      }
      snprintf(cover, sizeof cover, "%s/%s", folder, entry->d_name);
      if (!is_file(cover)) {
        continue;
      }

      /* NextUI drops the cart's last extension and MinUI keeps the whole name */
      snprintf(base, sizeof base, "%s", entry->d_name);
      dot = strrchr(base, '.');
      if (dot != NULL) {
        *dot = '\0';
      }
      snprintf(cover, sizeof cover, "%s/.media/%s.png", folder, base);
      changed += apply_cover(cover, chosen, other);
      snprintf(cover, sizeof cover, "%s/.res/%s.png", folder, entry->d_name);
      changed += apply_cover(cover, chosen, other);
    }
    closedir(carts);
  }

  closedir(roms);
  return changed;
}
