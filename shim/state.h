/*
 * Save states for pico-8, which has none of its own: the whole of its memory,
 * written to a file and put back.
 *
 * pico-8's memory is its executable's writable segment and its heap. Every
 * allocation pico-8 makes itself is served from an arena at a fixed address,
 * so the heap is one block that can be copied and put back where it was. The
 * shim's own allocations, and those of SDL and libc, stay on libc's heap and
 * are never part of a state. A pico-8 built as a position-independent
 * executable is started again with address randomisation off, so its image
 * lands at the same address each run.
 *
 * A pointer the old process held into SDL or libc means nothing to the new
 * one, so a word of the writable segment that held one keeps the value it has
 * now. The handles pico-8 opens, its window, renderer and audio, are such
 * words, and stay the ones this process opened.
 *
 * Every SDL_RenderPresent pico-8 makes comes from its main loop with no Lua
 * running, so a state is taken and put back from there. Like gfx.c, nothing
 * here calls SDL: sdl-display.c holds pico-8's audio while this runs and keeps
 * pico-8's clock going from the time a state was saved at.
 */

#ifndef STATE_H
#define STATE_H

#include <stdint.h>

/*
 * Whether states can be saved: PICO_PAK_STATES_DIR was set when pico-8
 * started, the executable is pico-8, and the arena is in place. Splore runs
 * without the variable, and its allocations go to libc as before.
 */
int state_available(void);

/* whether pico-8 has started its cart, which a resume waits for */
int state_cart_started(void);

/* whether pico-8 is downloading in the background, when a state cannot be taken */
int state_busy(void);

/*
 * Called on every frame pico-8 presents. The first once the cart has started
 * notes the big blocks pico-8 loaded before it, which a state leaves out for
 * as long as they stay as they were.
 */
void state_frame(void);

/*
 * A state is taken in two steps, so pico-8 only waits for the first: a copy
 * of its memory, for cart, with ticks the clock pico-8 reads, and then
 * writing that copy to path, which touches nothing of pico-8's and can run
 * on a thread of its own. state_capture returns NULL when no state can be
 * taken, and state_write 0 when it could not write, leaving any state
 * already at path as it was.
 */
struct state_snapshot;
struct state_snapshot *state_capture(const char *cart, uint32_t ticks);
int state_write(const struct state_snapshot *snapshot, const char *path);
void state_free(struct state_snapshot *snapshot);

/* both steps at once, for when pico-8 is about to exit */
int state_save(const char *path, const char *cart, uint32_t ticks);

/*
 * Puts back the state at path, which must have been saved for cart by this
 * same build of pico-8, and gives the clock it was saved at in ticks. Returns
 * 0 when it could not, with pico-8 untouched.
 */
int state_load(const char *path, const char *cart, uint32_t *ticks);

#endif
