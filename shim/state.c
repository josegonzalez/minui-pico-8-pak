/*
 * Save states. See state.h.
 */

#define _GNU_SOURCE
#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/personality.h>
#include <sys/stat.h>
#include <unistd.h>

#include "gfx.h"
#include "state.h"

/*
 * dlmalloc runs the arena, and keeps its own bookkeeping inside it, so a state
 * holds that too. It is given the whole arena up front and never asks for
 * more. Its warnings are its own, so they are silenced for the include alone,
 * and with the shim's hidden visibility none of it is exported.
 */
#define ONLY_MSPACES 1
#define MSPACES 1
#define HAVE_MMAP 0
#define HAVE_MORECORE 0
#define USE_LOCKS 1
#define NO_MALLOC_STATS 1
#define MALLOC_FAILURE_ACTION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#include "dlmalloc.c"
#pragma GCC diagnostic pop

/*
 * LZ4 packs a state to about a fifth of its size in tens of milliseconds on
 * these cores, where zlib took seconds. The SD cards on these devices write
 * at as little as 1.5MB a second, so what it saves in writing is most of a
 * save. It is built in hidden, like dlmalloc.
 */
#define LZ4LIB_VISIBILITY
/* dlmalloc's assert is an empty statement, where LZ4 needs one that is an expression */
#undef assert
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wempty-body"
#include "lz4.c"
#pragma GCC diagnostic pop

#define LOG(...) gfx_log(__VA_ARGS__)
#define EXPORT __attribute__((visibility("default")))

/*
 * Clear of where the kernel puts anything for pico-8, and on arm64 under
 * 2^38, so it fits the smallest address space these kernels are built with.
 * pico-8 uses about 25MB of it.
 */
#if UINTPTR_MAX > 0xffffffffu
#define ARENA_ADDRESS ((uintptr_t)0x2000000000ull)
#define ARENA_SIZE ((size_t)256 << 20)
#else
#define ARENA_ADDRESS ((uintptr_t)0x60000000u)
#define ARENA_SIZE ((size_t)96 << 20)
#endif

#define PAGE 4096
#define PAGE_UP(x) (((x) + PAGE - 1) & ~(uintptr_t)(PAGE - 1))

extern void *__libc_malloc(size_t size);
extern void *__libc_calloc(size_t count, size_t size);
extern void *__libc_realloc(void *pointer, size_t size);
extern void __libc_free(void *pointer);

static enum { STATE_UNDECIDED, STATE_ON, STATE_OFF } mode;

/* pico-8's image, where it is loaded this run */
static uintptr_t image_base, image_lo, image_hi, text_lo, text_hi, data_lo, data_hi;
static int position_independent;
static unsigned char build_id[20];
static size_t build_id_size;

/* the GOT holds this process's library addresses, which a state never replaces */
static uintptr_t got_lo, got_hi;
/* pico-8's machine, its RAM and registers, which holds no pointers and is put back whole */
static uintptr_t machine_lo, machine_hi;
/* the cart's Lua state, NULL until the cart starts, and the downloads running */
static uintptr_t cart_lua;
static uintptr_t download_threads;

static char *arena;
static mspace space;
/* set when the arena ran out, after which pico-8's memory is no longer all in it */
static int arena_full;

/* the address ranges of everything that is neither pico-8's image nor the arena */
struct range {
  uint64_t lo, hi;
};

/*
 * The big blocks pico-8 allocates before its cart starts, such as the 8.6MB it
 * reads from pico8.dat. Each is hashed once the cart is running, and one that
 * still matches when a state is saved is left out of it: every run of the same
 * pico-8 loads it the same, so the run a state is loaded into holds it too.
 */
#define BIG_BLOCK (1u << 20)
#define BIG_BLOCKS 16

static struct big_block {
  uintptr_t at;
  size_t size;
  uint64_t hash;
} big_blocks[BIG_BLOCKS];
static int big_blocks_hashed;

/* a block a state leaves out, by where it is in the arena and what it hashes to */
struct state_kept {
  uint64_t offset, size, hash;
};

/* written ahead of the memory, every field checked before any of it is put back */
#define STATE_MAGIC "P8STATE3"
#define STATE_CART_SIZE 1024

struct state_header {
  char magic[8];
  uint32_t build_id_size;
  unsigned char build_id[20];
  uint64_t data_lo, data_hi;
  uint64_t arena, arena_end;
  uint64_t range_count;
  uint64_t kept_count;
  /* the pages in the file, before and after packing */
  uint64_t stored_size, packed_size;
  uint32_t ticks;
  uint32_t page_size;
  char cart[STATE_CART_SIZE];
};

/* a state as taken, in memory, until it is written */
struct state_snapshot {
  struct state_header header;
  struct range *ranges;
  struct state_kept kept[BIG_BLOCKS];
  /* a byte for each page, and the pages in the file, one after another and packed */
  unsigned char *flags;
  size_t page_total;
  unsigned char *packed;
  size_t packed_capacity;
};

/*
 * The pages are packed in chunks of this much, each after its size before and
 * after, or kept as they are where packing would not make them smaller
 */
#define CHUNK (256u << 10)

/* what a page of a state is: all zero, in the file, or kept from the run it is loaded into */
enum { PAGE_ZERO, PAGE_STORED, PAGE_KEPT };

/* ---- finding pico-8 ---- */

static void read_build_id(const ElfW(Phdr) *phdr)
{
  const unsigned char *at = (const unsigned char *)(image_base + phdr->p_vaddr);
  const unsigned char *end = at + phdr->p_memsz;

  while (at + sizeof(ElfW(Nhdr)) <= end) {
    const ElfW(Nhdr) *note = (const void *)at;
    const unsigned char *desc = at + sizeof *note + ((note->n_namesz + 3) & ~3u);

    if (note->n_type == NT_GNU_BUILD_ID && note->n_namesz == 4 && memcmp(at + sizeof *note, "GNU", 4) == 0 &&
        note->n_descsz <= sizeof build_id) {
      memcpy(build_id, desc, note->n_descsz);
      build_id_size = note->n_descsz;
      return;
    }
    at = desc + ((note->n_descsz + 3) & ~3u);
  }
}

/* the first object the loader lists is the executable */
static int find_executable(struct dl_phdr_info *info, size_t size, void *data)
{
  uintptr_t relro_hi = 0;
  int i;

  (void)size;
  (void)data;
  image_base = info->dlpi_addr;
  for (i = 0; i < info->dlpi_phnum; i++) {
    const ElfW(Phdr) *phdr = &info->dlpi_phdr[i];
    uintptr_t lo = image_base + phdr->p_vaddr, hi = lo + phdr->p_memsz;

    if (phdr->p_type == PT_LOAD) {
      if (image_lo == 0 || lo < image_lo) {
        image_lo = lo & ~(uintptr_t)(PAGE - 1);
      }
      if (hi > image_hi) {
        image_hi = PAGE_UP(hi);
      }
      if (phdr->p_flags & PF_X) {
        text_lo = lo;
        text_hi = hi;
      } else if (phdr->p_flags & PF_W) {
        data_lo = lo;
        data_hi = hi;
      }
    } else if (phdr->p_type == PT_GNU_RELRO) {
      relro_hi = hi;
    } else if (phdr->p_type == PT_NOTE && build_id_size == 0) {
      read_build_id(phdr);
    }
  }

  /* RELRO is made read-only once relocated, so it is never put back */
  if (relro_hi > data_lo && relro_hi < data_hi) {
    data_lo = PAGE_UP(relro_hi);
  }
  return 1;
}

static void *read_at(int fd, off_t offset, size_t size)
{
  char *buffer = __libc_malloc(size + 1);

  if (buffer == NULL || pread(fd, buffer, size, offset) != (ssize_t)size) {
    __libc_free(buffer);
    return NULL;
  }
  buffer[size] = '\0';
  return buffer;
}

/*
 * pico-8 keeps its symbols, so its own names find what a state needs in any
 * build. A program without pstate is not pico-8, and is left alone.
 */
static int read_symbols(void)
{
  int fd = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
  ElfW(Ehdr) header;
  ElfW(Shdr) *sections = NULL;
  char *names = NULL;
  int found = 0, i;

  if (fd < 0 || pread(fd, &header, sizeof header, 0) != (ssize_t)sizeof header ||
      memcmp(header.e_ident, ELFMAG, SELFMAG) != 0 || header.e_shentsize != sizeof *sections) {
    goto done;
  }
  position_independent = header.e_type == ET_DYN;
  sections = read_at(fd, header.e_shoff, (size_t)header.e_shnum * sizeof *sections);
  if (sections == NULL || header.e_shstrndx >= header.e_shnum) {
    goto done;
  }
  names = read_at(fd, sections[header.e_shstrndx].sh_offset, sections[header.e_shstrndx].sh_size);
  if (names == NULL) {
    goto done;
  }

  for (i = 0; i < header.e_shnum; i++) {
    const ElfW(Shdr) *section = &sections[i];
    const char *name = section->sh_name < sections[header.e_shstrndx].sh_size ? names + section->sh_name : "";

    if (strcmp(name, ".got") == 0 || strcmp(name, ".got.plt") == 0) {
      uintptr_t lo = image_base + section->sh_addr, hi = lo + section->sh_size;
      got_lo = got_lo == 0 || lo < got_lo ? lo : got_lo;
      got_hi = hi > got_hi ? hi : got_hi;
    }

    if (section->sh_type == SHT_SYMTAB && section->sh_link < header.e_shnum &&
        section->sh_entsize == sizeof(ElfW(Sym))) {
      const ElfW(Shdr) *strings = &sections[section->sh_link];
      ElfW(Sym) *symbols = read_at(fd, section->sh_offset, section->sh_size);
      char *text = read_at(fd, strings->sh_offset, strings->sh_size);
      size_t s, count = section->sh_size / sizeof *symbols;

      for (s = 0; symbols != NULL && text != NULL && s < count; s++) {
        const char *symbol = symbols[s].st_name < strings->sh_size ? text + symbols[s].st_name : "";
        uintptr_t at = image_base + symbols[s].st_value;

        if (symbols[s].st_shndx == SHN_UNDEF) {
          continue;
        }
        if (strcmp(symbol, "pstate") == 0) {
          machine_lo = at;
          machine_hi = at + symbols[s].st_size;
          found = 1;
        } else if (strcmp(symbol, "L0_cart") == 0 && symbols[s].st_size == sizeof(void *)) {
          cart_lua = at;
        } else if (strcmp(symbol, "num_threads") == 0 && symbols[s].st_size == sizeof(int)) {
          download_threads = at;
        }
      }
      __libc_free(symbols);
      __libc_free(text);
    }
  }

done:
  __libc_free(names);
  __libc_free(sections);
  if (fd >= 0) {
    close(fd);
  }
  return found;
}

static int address_randomised(void)
{
  char value[8] = "";
  int fd;

  if (personality(0xffffffff) & ADDR_NO_RANDOMIZE) {
    return 0;
  }
  fd = open("/proc/sys/kernel/randomize_va_space", O_RDONLY | O_CLOEXEC);
  if (fd >= 0) {
    if (read(fd, value, sizeof value - 1) < 0) {
      value[0] = '\0';
    }
    close(fd);
  }
  return value[0] != '0';
}

/*
 * pico8_64 is position independent, so its image would move every run, and
 * every pointer into it with it. It is started again, once, with randomising
 * turned off, which returns only when that cannot be done.
 */
static void start_without_randomising(void)
{
  int fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
  char *arguments = __libc_malloc(65536);
  char **argv;
  ssize_t got = 0, n;
  int count = 0, i;
  unsigned current = (unsigned)personality(0xffffffff);

  if (fd < 0 || arguments == NULL) {
    if (fd >= 0) {
      close(fd);
    }
    __libc_free(arguments);
    return;
  }
  while (got < 65535 && (n = read(fd, arguments + got, 65535 - got)) > 0) {
    got += n;
  }
  close(fd);
  arguments[got] = '\0';
  for (i = 0; i < got; i++) {
    count += arguments[i] == '\0';
  }
  argv = __libc_calloc(count + 2, sizeof *argv);
  if (argv == NULL || got == 0) {
    __libc_free(arguments);
    __libc_free(argv);
    return;
  }
  for (i = 0, count = 0; i < got; i += strlen(arguments + i) + 1) {
    argv[count++] = arguments + i;
  }
  argv[count] = NULL;

  if (personality(current | ADDR_NO_RANDOMIZE) == -1) {
    LOG("cannot turn address randomising off, a state will only load in this run of pico-8\n");
  } else {
    LOG("starting pico-8 again with address randomising off, for save states\n");
    execv("/proc/self/exe", argv);
    LOG("cannot start pico-8 again, a state will only load in this run of pico-8\n");
    personality(current);
  }
  __libc_free(arguments);
  __libc_free(argv);
}

/*
 * Runs before pico-8's own code, so nothing pico-8 allocates is ever left
 * outside the arena. Every other process the variable reaches, launch.sh's
 * own commands among them, finds no pico-8 here and keeps libc's malloc.
 */
__attribute__((constructor)) static void state_init(void)
{
  const char *states_dir = getenv("PICO_PAK_STATES_DIR");
  void *placed;

  mode = STATE_OFF;
  if (states_dir == NULL || *states_dir == '\0') {
    return;
  }
  dl_iterate_phdr(find_executable, NULL);
  if (text_lo == 0 || data_lo == 0 || !read_symbols()) {
    return;
  }
  if (position_independent && address_randomised()) {
    start_without_randomising();
  }

  /* a hint, as the oldest of these kernels has no MAP_FIXED_NOREPLACE */
  placed = mmap((void *)ARENA_ADDRESS, ARENA_SIZE, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (placed != (void *)ARENA_ADDRESS) {
    if (placed != MAP_FAILED) {
      munmap(placed, ARENA_SIZE);
    }
    LOG("cannot place the save state arena at %p, save states are off\n", (void *)ARENA_ADDRESS);
    return;
  }
  space = create_mspace_with_base(placed, ARENA_SIZE, 1);
  if (space == NULL) {
    munmap(placed, ARENA_SIZE);
    LOG("cannot start the save state arena, save states are off\n");
    return;
  }
  arena = placed;
  mode = STATE_ON;
  LOG("save states on: pico-8's image at %p, its arena at %p\n", (void *)image_lo, (void *)arena);
}

/* ---- pico-8's allocations ---- */

static int in_arena(const void *pointer)
{
  return arena != NULL && (uintptr_t)pointer >= (uintptr_t)arena &&
         (uintptr_t)pointer < (uintptr_t)arena + ARENA_SIZE;
}

static int for_arena(const void *caller)
{
  return mode == STATE_ON && !arena_full && (uintptr_t)caller >= text_lo &&
         (uintptr_t)caller < text_hi;
}

/* pico-8 carries on without states rather than without memory */
static void arena_ran_out(size_t size)
{
  if (!arena_full) {
    LOG("the save state arena is full at %zu more bytes, save states are off\n", size);
  }
  arena_full = 1;
}

static void note_big_block(void *at, size_t size)
{
  int i;

  if (at == NULL || size < BIG_BLOCK || big_blocks_hashed) {
    return;
  }
  for (i = 0; i < BIG_BLOCKS; i++) {
    if (big_blocks[i].at == 0) {
      big_blocks[i].at = (uintptr_t)at;
      big_blocks[i].size = size;
      return;
    }
  }
}

static void forget_big_block(const void *at)
{
  int i;

  for (i = 0; i < BIG_BLOCKS; i++) {
    if (big_blocks[i].at == (uintptr_t)at) {
      big_blocks[i].at = 0;
    }
  }
}

static void *allocate(const void *caller, size_t count, size_t size, int clear)
{
  void *got;

  if (for_arena(caller)) {
    got = clear ? mspace_calloc(space, count, size) : mspace_malloc(space, size);
    if (got != NULL || (clear ? count * size : size) == 0) {
      note_big_block(got, clear ? count * size : size);
      return got;
    }
    arena_ran_out(clear ? count * size : size);
  }
  return clear ? __libc_calloc(count, size) : __libc_malloc(size);
}

EXPORT void *malloc(size_t size)
{
  return allocate(__builtin_return_address(0), 1, size, 0);
}

EXPORT void *calloc(size_t count, size_t size)
{
  return allocate(__builtin_return_address(0), count, size, 1);
}

EXPORT void *realloc(void *pointer, size_t size)
{
  void *got;

  if (pointer == NULL) {
    return allocate(__builtin_return_address(0), 1, size, 0);
  }
  if (!in_arena(pointer)) {
    return __libc_realloc(pointer, size);
  }

  forget_big_block(pointer);
  got = mspace_realloc(space, pointer, size);
  if (got != NULL || size == 0) {
    return got;
  }
  arena_ran_out(size);
  got = __libc_malloc(size);
  if (got != NULL) {
    size_t had = mspace_usable_size(pointer);
    memcpy(got, pointer, had < size ? had : size);
    mspace_free(space, pointer);
  }
  return got;
}

EXPORT void free(void *pointer)
{
  if (in_arena(pointer)) {
    forget_big_block(pointer);
    mspace_free(space, pointer);
    return;
  }
  __libc_free(pointer);
}

/* ---- the state ---- */

int state_available(void)
{
  return mode == STATE_ON && !arena_full;
}

int state_cart_started(void)
{
  return state_available() && cart_lua != 0 && *(void *const *)cart_lua != NULL;
}

int state_busy(void)
{
  return download_threads != 0 && *(const int *)download_threads > 0;
}

/* FNV-1a over whole words, which on these cores hashes 8.6MB in a few milliseconds */
static uint64_t hash_bytes(const unsigned char *at, size_t size)
{
  uint64_t hash = 0xcbf29ce484222325ull, word;
  size_t i;

  for (i = 0; i + sizeof word <= size; i += sizeof word) {
    memcpy(&word, at + i, sizeof word);
    hash = (hash ^ word) * 0x100000001b3ull;
  }
  for (; i < size; i++) {
    hash = (hash ^ at[i]) * 0x100000001b3ull;
  }
  return hash;
}

void state_frame(void)
{
  size_t total = 0;
  int i;

  if (big_blocks_hashed || !state_cart_started()) {
    return;
  }
  for (i = 0; i < BIG_BLOCKS; i++) {
    if (big_blocks[i].at != 0) {
      big_blocks[i].hash = hash_bytes((const unsigned char *)big_blocks[i].at, big_blocks[i].size);
      total += big_blocks[i].size;
    }
  }
  big_blocks_hashed = 1;
  LOG("%zu KB of pico-8's own data can be left out of its states\n", total >> 10);
}

/* everything mapped that is neither pico-8's image nor the arena, from /proc/self/maps */
static struct range *read_foreign(uint64_t *count)
{
  FILE *maps = fopen("/proc/self/maps", "r");
  struct range *ranges = NULL, *grown;
  size_t capacity = 0;
  char line[512];

  *count = 0;
  if (maps == NULL) {
    return NULL;
  }
  while (fgets(line, sizeof line, maps) != NULL) {
    unsigned long lo, hi;

    if (sscanf(line, "%lx-%lx", &lo, &hi) != 2) {
      continue;
    }
    /* the brk heap can be merged with the end of the image's bss */
    if (lo < image_hi && hi > image_lo) {
      if (hi <= image_hi) {
        continue;
      }
      lo = image_hi;
    }
    if (in_arena((void *)lo)) {
      continue;
    }
    if (*count == capacity) {
      capacity = capacity ? capacity * 2 : 256;
      grown = __libc_realloc(ranges, capacity * sizeof *ranges);
      if (grown == NULL) {
        break;
      }
      ranges = grown;
    }
    ranges[*count].lo = lo;
    ranges[*count].hi = hi;
    (*count)++;
  }
  fclose(maps);
  return ranges;
}

static int is_foreign(uint64_t value, const struct range *ranges, uint64_t count)
{
  uint64_t i;

  for (i = 0; i < count; i++) {
    if (value >= ranges[i].lo && value < ranges[i].hi) {
      return 1;
    }
  }
  return 0;
}

/* the part of the arena dlmalloc has handed out, up to and including the header of its top chunk */
static uintptr_t arena_end(void)
{
  return PAGE_UP((uintptr_t)((mstate)space)->top + TOP_FOOT_SIZE);
}

static int is_zero_page(const unsigned char *page, size_t size)
{
  static const unsigned char zero[PAGE];

  return memcmp(page, zero, size) == 0;
}

/*
 * The writable segment and then the arena, in pages: a byte for each saying
 * whether it is zero, in the file, or kept from the run it is loaded into,
 * and then only the pages in the file, as a third of a state is zero and
 * another third is pico8.dat.
 */
struct region {
  unsigned char *at;
  size_t size;
};

static size_t page_size_at(size_t region_size, size_t page)
{
  size_t left = region_size - page * PAGE;

  return left < PAGE ? left : PAGE;
}

static int page_is_kept(const struct state_snapshot *snapshot, uintptr_t page)
{
  uint64_t i;

  for (i = 0; i < snapshot->header.kept_count; i++) {
    uintptr_t lo = (uintptr_t)arena + snapshot->kept[i].offset, hi = lo + snapshot->kept[i].size;
    if (page >= lo && page + PAGE <= hi) {
      return 1;
    }
  }
  return 0;
}

/* the big blocks still as they were when the cart started */
static void find_kept(struct state_snapshot *snapshot)
{
  int i;

  if (!big_blocks_hashed) {
    return;
  }
  for (i = 0; i < BIG_BLOCKS; i++) {
    const struct big_block *block = &big_blocks[i];
    struct state_kept *kept = &snapshot->kept[snapshot->header.kept_count];

    if (block->at == 0 ||
        hash_bytes((const unsigned char *)block->at, block->size) != block->hash) {
      continue;
    }
    kept->offset = block->at - (uintptr_t)arena;
    kept->size = block->size;
    kept->hash = block->hash;
    snapshot->header.kept_count++;
  }
}

void state_free(struct state_snapshot *snapshot)
{
  if (snapshot == NULL) {
    return;
  }
  __libc_free(snapshot->ranges);
  __libc_free(snapshot->flags);
  __libc_free(snapshot->packed);
  __libc_free(snapshot);
}

static int pack_chunk(struct state_snapshot *snapshot, const unsigned char *raw, size_t size)
{
  size_t need = snapshot->header.packed_size + 2 * sizeof(uint32_t) + LZ4_compressBound((int)size);
  uint32_t sizes[2];
  unsigned char *at;
  int packed;

  if (need > snapshot->packed_capacity) {
    size_t capacity = need + need / 2;
    unsigned char *grown = __libc_realloc(snapshot->packed, capacity);
    if (grown == NULL) {
      return 0;
    }
    snapshot->packed = grown;
    snapshot->packed_capacity = capacity;
  }

  at = snapshot->packed + snapshot->header.packed_size + sizeof sizes;
  packed = LZ4_compress_default((const char *)raw, (char *)at, (int)size, LZ4_compressBound((int)size));
  sizes[0] = (uint32_t)size;
  sizes[1] = packed > 0 && (size_t)packed < size ? (uint32_t)packed : 0;
  if (sizes[1] == 0) {
    memcpy(at, raw, size);
  }
  memcpy(snapshot->packed + snapshot->header.packed_size, sizes, sizeof sizes);
  snapshot->header.packed_size += sizeof sizes + (sizes[1] != 0 ? sizes[1] : sizes[0]);
  snapshot->header.stored_size += size;
  return 1;
}

struct state_snapshot *state_capture(const char *cart, uint32_t ticks)
{
  struct state_snapshot *snapshot;
  struct region regions[2];
  unsigned char *chunk;
  size_t page, at = 0, filled = 0;
  int r, ok;

  if (!state_available() || state_busy() || cart == NULL || strlen(cart) >= STATE_CART_SIZE) {
    return NULL;
  }
  snapshot = __libc_calloc(1, sizeof *snapshot);
  if (snapshot == NULL) {
    return NULL;
  }

  memcpy(snapshot->header.magic, STATE_MAGIC, sizeof snapshot->header.magic);
  snapshot->header.build_id_size = (uint32_t)build_id_size;
  memcpy(snapshot->header.build_id, build_id, build_id_size);
  snapshot->header.data_lo = data_lo;
  snapshot->header.data_hi = data_hi;
  snapshot->header.arena = (uintptr_t)arena;
  snapshot->header.arena_end = arena_end();
  snapshot->header.ticks = ticks;
  snapshot->header.page_size = PAGE;
  snprintf(snapshot->header.cart, sizeof snapshot->header.cart, "%s", cart);
  snapshot->ranges = read_foreign(&snapshot->header.range_count);
  find_kept(snapshot);

  regions[0].at = (unsigned char *)data_lo;
  regions[0].size = data_hi - data_lo;
  regions[1].at = (unsigned char *)arena;
  regions[1].size = snapshot->header.arena_end - (uintptr_t)arena;
  for (r = 0; r < 2; r++) {
    snapshot->page_total += (regions[r].size + PAGE - 1) / PAGE;
  }
  snapshot->flags = __libc_malloc(snapshot->page_total);
  if (snapshot->ranges == NULL || snapshot->flags == NULL) {
    state_free(snapshot);
    return NULL;
  }

  for (r = 0; r < 2; r++) {
    for (page = 0; page * PAGE < regions[r].size; page++) {
      unsigned char *from = regions[r].at + page * PAGE;
      size_t size = page_size_at(regions[r].size, page);

      if (r == 1 && size == PAGE && page_is_kept(snapshot, (uintptr_t)from)) {
        snapshot->flags[at++] = PAGE_KEPT;
      } else if (is_zero_page(from, size)) {
        snapshot->flags[at++] = PAGE_ZERO;
      } else {
        snapshot->flags[at++] = PAGE_STORED;
      }
    }
  }

  /*
   * Only the pages that go in the file are copied, packed as they go, so
   * pico-8 is held for as short a time as can be and the copy is small
   */
  chunk = __libc_malloc(CHUNK);
  ok = chunk != NULL;
  at = 0;
  for (r = 0; ok && r < 2; r++) {
    for (page = 0; ok && page * PAGE < regions[r].size; page++) {
      size_t size = page_size_at(regions[r].size, page);

      if (snapshot->flags[at++] != PAGE_STORED) {
        continue;
      }
      if (filled + size > CHUNK) {
        ok = pack_chunk(snapshot, chunk, filled);
        filled = 0;
      }
      memcpy(chunk + filled, regions[r].at + page * PAGE, size);
      filled += size;
    }
  }
  if (ok && filled > 0) {
    ok = pack_chunk(snapshot, chunk, filled);
  }
  __libc_free(chunk);
  if (!ok) {
    state_free(snapshot);
    return NULL;
  }
  return snapshot;
}

/*
 * Writes under a temporary name and puts it in place, so a write cut short
 * never replaces the state already there. It reads nothing of pico-8's, so it
 * can run while pico-8 carries on.
 */
int state_write(const struct state_snapshot *snapshot, const char *path)
{
  char temporary[4096];
  uint64_t kept = 0, i;
  FILE *file;
  int ok;

  if (snapshot == NULL || path == NULL ||
      snprintf(temporary, sizeof temporary, "%s.part", path) >= (int)sizeof temporary) {
    return 0;
  }
  file = fopen(temporary, "wb");
  ok = file != NULL && fwrite(&snapshot->header, sizeof snapshot->header, 1, file) == 1 &&
       fwrite(snapshot->ranges, sizeof *snapshot->ranges, snapshot->header.range_count, file) ==
           snapshot->header.range_count &&
       fwrite(snapshot->kept, sizeof *snapshot->kept, snapshot->header.kept_count, file) ==
           snapshot->header.kept_count &&
       fwrite(snapshot->flags, 1, snapshot->page_total, file) == snapshot->page_total &&
       fwrite(snapshot->packed, 1, snapshot->header.packed_size, file) ==
           snapshot->header.packed_size;
  if (file != NULL) {
    ok = fclose(file) == 0 && ok;
  }
  if (!ok || rename(temporary, path) != 0) {
    LOG("cannot write the state %s\n", path);
    unlink(temporary);
    return 0;
  }
  for (i = 0; i < snapshot->header.kept_count; i++) {
    kept += snapshot->kept[i].size;
  }
  LOG("saved the state %s, %lu KB packed to %lu KB, leaving out %lu KB of pico-8's own data\n", path,
      (unsigned long)(snapshot->header.stored_size >> 10),
      (unsigned long)(snapshot->header.packed_size >> 10), (unsigned long)(kept >> 10));
  return 1;
}

int state_save(const char *path, const char *cart, uint32_t ticks)
{
  struct state_snapshot *snapshot = state_capture(cart, ticks);
  int ok = state_write(snapshot, path);

  state_free(snapshot);
  return ok;
}

/* everything in the header has to match this run of pico-8 before anything is put back */
static int header_matches(const struct state_header *header, const char *path, const char *cart)
{
  const char *why = NULL;

  if (memcmp(header->magic, STATE_MAGIC, sizeof header->magic) != 0 || header->page_size != PAGE) {
    why = "is not a state this pak can load";
  } else if (header->build_id_size != build_id_size ||
             memcmp(header->build_id, build_id, build_id_size) != 0) {
    why = "was saved by another build of pico-8";
  } else if (header->data_lo != data_lo || header->data_hi != data_hi ||
             header->arena != (uintptr_t)arena) {
    why = "was saved with pico-8 at another address";
  } else if (header->arena_end <= header->arena || header->arena_end > header->arena + ARENA_SIZE ||
             header->range_count > 1u << 20 || header->kept_count > BIG_BLOCKS) {
    why = "is damaged";
  } else if (memchr(header->cart, '\0', sizeof header->cart) == NULL ||
             strcmp(header->cart, cart) != 0) {
    why = "is for another cart";
  }

  if (why != NULL) {
    LOG("the state %s %s, not loading it\n", path, why);
    return 0;
  }
  return 1;
}

/*
 * What a state left out has to be in this run of pico-8 as it was in the one
 * that saved it, though maybe somewhere else in the arena, as pico-8 reads its
 * config before pico8.dat and a different config moves everything after it.
 * Finds where each block is now.
 */
static int find_kept_sources(const struct state_kept *kept, uint64_t count, const char *path,
                             const unsigned char **sources)
{
  uint64_t i;
  int b;

  for (i = 0; i < count; i++) {
    sources[i] = NULL;
    if (kept[i].size > ARENA_SIZE || kept[i].offset > ARENA_SIZE - kept[i].size) {
      LOG("the state %s is damaged, not loading it\n", path);
      return 0;
    }
    for (b = 0; b < BIG_BLOCKS && sources[i] == NULL && big_blocks_hashed; b++) {
      if (big_blocks[b].at != 0 && big_blocks[b].size == kept[i].size &&
          big_blocks[b].hash == kept[i].hash &&
          hash_bytes((const unsigned char *)big_blocks[b].at, big_blocks[b].size) == kept[i].hash) {
        sources[i] = (const unsigned char *)big_blocks[b].at;
      }
    }
    if (sources[i] == NULL) {
      LOG("the state %s left out pico-8's data this run does not hold, not loading it\n", path);
      return 0;
    }
  }
  return 1;
}

/* where the page at offset of the arena comes from in this run, for a page a state left out */
static const unsigned char *kept_source(const struct state_kept *kept, const unsigned char *const *sources,
                                        uint64_t count, size_t offset)
{
  uint64_t i;

  for (i = 0; i < count; i++) {
    if (offset >= kept[i].offset && offset + PAGE <= kept[i].offset + kept[i].size) {
      return sources[i] + (offset - kept[i].offset);
    }
  }
  return NULL;
}

/* the pages in the file, unpacked one chunk after another into stored */
static int unpack(const unsigned char *packed, size_t packed_size, unsigned char *stored,
                  size_t stored_size)
{
  size_t in = 0, out = 0;
  uint32_t sizes[2];

  while (in < packed_size) {
    if (packed_size - in < sizeof sizes) {
      return 0;
    }
    memcpy(sizes, packed + in, sizeof sizes);
    in += sizeof sizes;
    if (sizes[0] > CHUNK || sizes[0] > stored_size - out ||
        (sizes[1] != 0 ? sizes[1] : sizes[0]) > packed_size - in) {
      return 0;
    }
    if (sizes[1] == 0) {
      memcpy(stored + out, packed + in, sizes[0]);
      in += sizes[0];
    } else {
      if (LZ4_decompress_safe((const char *)packed + in, (char *)stored + out, (int)sizes[1],
                              (int)sizes[0]) != (int)sizes[0]) {
        return 0;
      }
      in += sizes[1];
    }
    out += sizes[0];
  }
  return out == stored_size;
}

static int read_pages(FILE *file, const struct state_header *header, unsigned char **into,
                      const size_t *sizes, int count, const struct state_kept *kept,
                      const unsigned char *const *sources, uint64_t kept_count)
{
  size_t total = 0, page, at = 0, used = 0;
  unsigned char *flags, *packed = NULL, *stored = NULL;
  int r, ok;

  for (r = 0; r < count; r++) {
    total += (sizes[r] + PAGE - 1) / PAGE;
  }
  if (header->stored_size > sizes[0] + sizes[1] ||
      header->packed_size > header->stored_size + header->stored_size / 8 + 65536) {
    return 0;
  }
  flags = __libc_malloc(total);
  packed = __libc_malloc(header->packed_size + 1);
  stored = __libc_malloc(header->stored_size + 1);
  ok = flags != NULL && packed != NULL && stored != NULL && fread(flags, 1, total, file) == total &&
       fread(packed, 1, header->packed_size, file) == header->packed_size &&
       unpack(packed, header->packed_size, stored, header->stored_size);
  __libc_free(packed);
  for (r = 0; ok && r < count; r++) {
    for (page = 0; ok && page * PAGE < sizes[r]; page++) {
      size_t size = page_size_at(sizes[r], page);
      unsigned char *to = into[r] + page * PAGE;

      switch (flags[at++]) {
      case PAGE_STORED:
        ok = size <= header->stored_size - used;
        if (ok) {
          memcpy(to, stored + used, size);
          used += size;
        }
        break;
      case PAGE_KEPT: {
        const unsigned char *from =
            r == 1 ? kept_source(kept, sources, kept_count, page * PAGE) : NULL;
        if (from == NULL) {
          ok = 0;
        } else {
          memcpy(to, from, size);
        }
        break;
      }
      default:
        memset(to, 0, size);
        break;
      }
    }
  }
  __libc_free(flags);
  __libc_free(stored);
  return ok && used == header->stored_size;
}

/*
 * A word that pointed outside pico-8 when the state was saved keeps the value
 * it has now, unless what is there now is not such a pointer, which makes the
 * saved one only a number that happened to look like one.
 */
static void put_back_data(const unsigned char *saved, const struct range *old_ranges,
                          uint64_t old_count, const struct range *ranges, uint64_t count)
{
  size_t offset, size = data_hi - data_lo, kept = 0;

  for (offset = 0; offset + sizeof(uintptr_t) <= size; offset += sizeof(uintptr_t)) {
    uintptr_t at = data_lo + offset, was, now;

    memcpy(&was, saved + offset, sizeof was);
    memcpy(&now, (const void *)at, sizeof now);
    if (at >= got_lo && at < got_hi) {
      continue;
    }
    if ((at < machine_lo || at >= machine_hi) && is_foreign(was, old_ranges, old_count) &&
        (now == 0 || is_foreign(now, ranges, count))) {
      kept++;
      continue;
    }
    memcpy((void *)at, &was, sizeof was);
  }
  /* the bytes after the last whole word */
  memcpy((void *)(data_lo + offset), saved + offset, size - offset);
  LOG("kept %zu of pico-8's pointers to this run's libraries\n", kept);
}

int state_load(const char *path, const char *cart, uint32_t *ticks)
{
  struct state_header header;
  struct state_kept kept[BIG_BLOCKS];
  const unsigned char *sources[BIG_BLOCKS];
  struct range *old_ranges = NULL, *ranges = NULL;
  unsigned char *staged[2] = {NULL, NULL};
  size_t sizes[2];
  uint64_t count = 0, i;
  uintptr_t end_now;
  FILE *file;
  int ok;

  if (!state_available() || state_busy() || path == NULL || cart == NULL) {
    return 0;
  }
  file = fopen(path, "rb");
  if (file == NULL) {
    LOG("cannot open the state %s\n", path);
    return 0;
  }

  ok = fread(&header, sizeof header, 1, file) == 1 && header_matches(&header, path, cart);
  if (ok) {
    sizes[0] = data_hi - data_lo;
    sizes[1] = header.arena_end - header.arena;
    old_ranges = __libc_malloc((header.range_count + 1) * sizeof *old_ranges);
    staged[0] = __libc_malloc(sizes[0]);
    staged[1] = __libc_malloc(sizes[1]);
    ok = old_ranges != NULL && staged[0] != NULL && staged[1] != NULL &&
         fread(old_ranges, sizeof *old_ranges, header.range_count, file) == header.range_count &&
         fread(kept, sizeof *kept, header.kept_count, file) == header.kept_count;
    if (ok) {
      ok = find_kept_sources(kept, header.kept_count, path, sources);
      if (ok && !read_pages(file, &header, staged, sizes, 2, kept, sources, header.kept_count)) {
        LOG("cannot read the state %s, it is cut short\n", path);
        ok = 0;
      }
    } else {
      LOG("cannot read the state %s, it is cut short\n", path);
    }
  }
  fclose(file);

  if (ok) {
    ranges = read_foreign(&count);
    ok = ranges != NULL;
  }
  if (ok) {
    end_now = arena_end();
    put_back_data(staged[0], old_ranges, header.range_count, ranges, count);
    memcpy(arena, staged[1], sizes[1]);
    /* what pico-8 had used past the state's end is free now, and zero as it was then */
    if (end_now > header.arena_end) {
      memset((void *)(uintptr_t)header.arena_end, 0, end_now - header.arena_end);
    }
    /* the big blocks are now where the state had them */
    memset(big_blocks, 0, sizeof big_blocks);
    for (i = 0; i < header.kept_count; i++) {
      big_blocks[i].at = (uintptr_t)arena + kept[i].offset;
      big_blocks[i].size = kept[i].size;
      big_blocks[i].hash = kept[i].hash;
    }
    *ticks = header.ticks;
    LOG("loaded the state %s\n", path);
  }

  __libc_free(old_ranges);
  __libc_free(ranges);
  __libc_free(staged[0]);
  __libc_free(staged[1]);
  return ok;
}
