/*
 * Masks SDL_INIT_SENSOR out of SDL initialisation.
 *
 * pico-8 asks SDL to start every subsystem it knows about and aborts when any
 * one of them will not start. NextUI builds SDL2 without sensor support, so on
 * h700 the sensor subsystem answers "SDL not built with sensor support" and
 * pico-8 exits with "** FATAL ERROR: Unable to initialize SDL" before drawing
 * anything.
 *
 * Dropping the flag is enough because pico-8 never reads a sensor. Doing it
 * from a preloaded object rather than by shipping another SDL2 is what keeps
 * NextUI's own build in play: on h700 that build is the only reason SDL
 * enumerates the built-in gpio-keys pad, which controllers/h700.txt maps by
 * GUID as "Deeplay-keys". A replacement SDL2 would trade the crash for a dead
 * pad.
 *
 * dlsym is the only thing this asks of the system, and on the pinned build
 * image that resolves at the base glibc version for aarch64, so the object
 * loads on a device far older than the one it was compiled on. Adding anything
 * that pulls in a newer symbol would undo that. See shim/Dockerfile.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>

/* SDL_INIT_SENSOR, from SDL.h. Not included so the shim builds without SDL. */
#define SDL_INIT_SENSOR 0x00008000u

static void *real_symbol(const char *name)
{
  return dlsym(RTLD_NEXT, name);
}

static int forward_init(const char *name, uint32_t flags)
{
  int (*real)(uint32_t) = (int (*)(uint32_t))real_symbol(name);

  if (real == NULL) {
    return -1;
  }

  return real(flags & ~SDL_INIT_SENSOR);
}

/*
 * The load-bearing wrapper. pico-8 links SDL2 directly, so its call binds to
 * this definition and dlsym(RTLD_NEXT) then reaches the real one.
 */
int SDL_Init(uint32_t flags)
{
  return forward_init("SDL_Init", flags);
}

/*
 * Wrapped for a caller that reaches it directly. Nothing here relies on
 * catching SDL2's own internal SDL_Init to SDL_InitSubSystem call, which need
 * not go through a symbol this object can interpose.
 */
int SDL_InitSubSystem(uint32_t flags)
{
  return forward_init("SDL_InitSubSystem", flags);
}

/* Defensive only: SDL2 refcounts and no-ops a subsystem that never started. */
void SDL_QuitSubSystem(uint32_t flags)
{
  void (*real)(uint32_t) = (void (*)(uint32_t))real_symbol("SDL_QuitSubSystem");

  if (real != NULL) {
    real(flags & ~SDL_INIT_SENSOR);
  }
}

/*
 * SDL_WasInit(0) means "report every subsystem", so the flag cannot simply be
 * masked off: a caller asking only about the sensor would be handed the whole
 * set instead of the "not initialised" answer it is owed.
 */
uint32_t SDL_WasInit(uint32_t flags)
{
  uint32_t (*real)(uint32_t) = (uint32_t (*)(uint32_t))real_symbol("SDL_WasInit");

  if (real == NULL) {
    return 0;
  }

  if (flags == 0) {
    return real(0) & ~SDL_INIT_SENSOR;
  }

  if ((flags & ~SDL_INIT_SENSOR) == 0) {
    return 0;
  }

  return real(flags & ~SDL_INIT_SENSOR);
}
