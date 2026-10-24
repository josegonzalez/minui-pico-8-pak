# syntax=docker/dockerfile:1
# Builds the SDL sensor shim for h700, which is always a 64-bit userland, and
# the display shim for both architectures launch.sh picks between: arm64 for
# h700, my355, tg5040 and tg5050, and arm for rg35xxplus, whose MinUI userland
# is 32-bit.
#
# The base image is the one detail that has to be right, and the ceiling matters
# more than the floor: glibc 2.34 folded libdl into libc, so anything built on a
# newer image records dlsym@GLIBC_2.34 and refuses to load on the older glibc
# these devices run. This image is glibc 2.31 and resolves dlsym out of
# libdl.so.2 at GLIBC_2.17 on aarch64 and GLIBC_2.4 on armhf, the base version
# of each, and no more than bin/h700/minui-presenter already asks for. Anything
# bookworm based, gcc 12 included, is too new.
#
# It also ships gcc already, so the build installs no packages and cannot break
# when Debian moves an end of life release off its mirrors. The assertions below
# are what catch a bump that breaks any of this.
ARG BASE_IMAGE=gcc:10-bullseye

# The display shim decodes overlays and draws the menu's text itself rather
# than relying on an SDL2_image or SDL2_ttf the device may not have, so both
# decoders are pinned by commit and checksum.
FROM scratch AS stb
ADD --checksum=sha256:594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3 \
  https://raw.githubusercontent.com/nothings/stb/003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5/stb_image.h \
  /stb_image.h
ADD --checksum=sha256:ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab \
  https://raw.githubusercontent.com/nothings/stb/003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5/stb_truetype.h \
  /stb_truetype.h
ADD --checksum=sha256:cbd5f0ad7a9cf4468affb36354a1d2338034f2c12473cf1a8e32053cb6914a05 \
  https://raw.githubusercontent.com/nothings/stb/003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5/stb_image_write.h \
  /stb_image_write.h

# Save states run pico-8's allocations from an arena of their own, which Doug
# Lea's malloc 2.8.6, public domain, manages. Its author's site is the only
# home it has, so it is pinned by checksum alone.
FROM scratch AS dlmalloc
ADD --checksum=sha256:be7be56b40b4b31417e74e77687f9b96127ea6cae534cf236aee40a4c739508d \
  https://gee.cs.oswego.edu/pub/misc/malloc.c \
  /dlmalloc.c

# LZ4, BSD licensed, packs each save state as it is taken.
FROM scratch AS lz4
ADD --checksum=sha256:9396f7de527bc8435de9c7569fb7998e56545a84b4f3c2d808c0235c01774539 \
  https://raw.githubusercontent.com/lz4/lz4/ebb370ca83af193212df4dcbadcc5d87bc0de2f0/lib/lz4.c \
  /lz4.c
ADD --checksum=sha256:26b82efc53d1570f3b54eef02e9c4764c1ad374ff03cac04e2ced5ea4d4c552f \
  https://raw.githubusercontent.com/lz4/lz4/ebb370ca83af193212df4dcbadcc5d87bc0de2f0/lib/lz4.h \
  /lz4.h

# The fonts the menu finds on the device, MinUI's CFF one and NextUI's
# TrueType one, for the menu tests alone: neither may ship with the pak.
FROM scratch AS fonts
ADD --checksum=sha256:33d0f588ec40928dbc62ec2a90bb248ec6fcab1d63f82b0275cf9e7f59facf92 \
  https://raw.githubusercontent.com/shauninman/MinUI/53e0296ea5a2794290fb5783765af6cee0063445/skeleton/SYSTEM/res/BPreplayBold-unhinted.otf \
  /BPreplayBold-unhinted.otf
ADD --checksum=sha256:b653557e79afa5c63ac5fd2a8c4e16c1a55c5addb6cbaf767b4bf4e57c144056 \
  https://raw.githubusercontent.com/LoveRetro/NextUI/be751f7812bcfc1181b2f0da285bfcb02a5d57c3/skeleton/SYSTEM/res/font1.ttf \
  /font1.ttf

FROM --platform=linux/arm64 ${BASE_IMAGE} AS build-arm64

COPY sdl-nosensor.c /src/sdl-nosensor.c
COPY sdl-display.c menu.c menu.h gfx.c gfx.h cart.c cart.h crash.c crash.h state.c state.h \
  shader.c shader.h shader-gl.c shader-gl.h /src/
COPY --from=stb /stb_image.h /stb_truetype.h /stb_image_write.h /src/
COPY --from=dlmalloc /dlmalloc.c /src/
COPY --from=lz4 /lz4.c /lz4.h /src/

RUN gcc -shared -fPIC -O2 -Wall -Wextra -Werror \
  -o /out.so /src/sdl-nosensor.c -ldl

# Hidden by default, so only the SDL wrappers and malloc's family are exported
# and nothing of the menu can be bound to a function of the same name in pico-8.
RUN gcc -shared -fPIC -fvisibility=hidden -O2 -Wall -Wextra -Werror \
  -o /display.so /src/sdl-display.c /src/menu.c /src/gfx.c /src/cart.c /src/crash.c \
  /src/state.c /src/shader.c /src/shader-gl.c -ldl -lpthread

# The display shim has to export every wrapper and nothing of the decoders, the
# menu or the save state arena, must not go looking for an SDL2_image at
# runtime, and must take nothing from libm, whose symbols carry glibc versions
# newer than the devices'.
RUN readelf -V /display.so | grep -q 'GLIBC_2.17' \
  && ! readelf -V /display.so | grep -Eq 'GLIBC_2\.(1[89]|[2-9][0-9])' \
  && readelf --dyn-syms /display.so | grep -q ' SDL_RenderPresent$' \
  && readelf --dyn-syms /display.so | grep -q ' SDL_RenderCopy$' \
  && readelf --dyn-syms /display.so | grep -q ' SDL_UpdateTexture$' \
  && readelf --dyn-syms /display.so | grep -q ' SDL_PollEvent$' \
  && readelf -W --dyn-syms /display.so | grep -q ' SDL_GameControllerGetButton$' \
  && readelf -W --dyn-syms /display.so | grep -q ' SDL_GameControllerGetAxis$' \
  && readelf --dyn-syms /display.so | grep -q ' SDL_DestroyRenderer$' \
  && readelf -W --dyn-syms /display.so | grep -q ' SDL_OpenAudioDevice$' \
  && readelf --dyn-syms /display.so | grep -q ' SDL_OpenAudio$' \
  && readelf --dyn-syms /display.so | grep -q ' SDL_GetModState$' \
  && readelf --dyn-syms /display.so | grep -q ' SDL_GetTicks$' \
  && readelf -W --dyn-syms /display.so | grep -Eq ' FUNC +GLOBAL +DEFAULT +[0-9]+ malloc$' \
  && readelf -W --dyn-syms /display.so | grep -Eq ' FUNC +GLOBAL +DEFAULT +[0-9]+ calloc$' \
  && readelf -W --dyn-syms /display.so | grep -Eq ' FUNC +GLOBAL +DEFAULT +[0-9]+ realloc$' \
  && readelf -W --dyn-syms /display.so | grep -Eq ' FUNC +GLOBAL +DEFAULT +[0-9]+ free$' \
  && ! readelf -W --dyn-syms /display.so | grep -Eq 'stbi|stbtt|mspace|LZ4| (gfx|menu|cart|crash|state|shader)_' \
  && ! readelf -W --dyn-syms /display.so | grep -Eq ' (sqrt|pow|cos|acos|fmod|floor|ceil|fabs|frexp|ldexp)f?(@|$)' \
  && ! readelf -d /display.so | grep -q 'libm\.so' \
  && ! strings /display.so | grep -q 'libSDL2_image'

# The object has to ask for nothing newer than the base glibc version, or a
# device with an older libc drops the preload with only a line in the log, and
# it has to export all four wrappers or it masks nothing.
RUN readelf -V /out.so | grep -q 'GLIBC_2.17' \
  && ! readelf -V /out.so | grep -Eq 'GLIBC_2\.(1[89]|[2-9][0-9])' \
  && readelf -d /out.so | grep -q 'libdl.so.2' \
  && readelf --dyn-syms /out.so | grep -q ' SDL_Init$' \
  && readelf --dyn-syms /out.so | grep -q ' SDL_InitSubSystem$' \
  && readelf --dyn-syms /out.so | grep -q ' SDL_QuitSubSystem$' \
  && readelf --dyn-syms /out.so | grep -q ' SDL_WasInit$'

FROM --platform=linux/arm/v7 ${BASE_IMAGE} AS build-arm

COPY sdl-display.c menu.c menu.h gfx.c gfx.h cart.c cart.h crash.c crash.h state.c state.h \
  shader.c shader.h shader-gl.c shader-gl.h /src/
COPY --from=stb /stb_image.h /stb_truetype.h /stb_image_write.h /src/
COPY --from=dlmalloc /dlmalloc.c /src/
COPY --from=lz4 /lz4.c /lz4.h /src/

RUN gcc -shared -fPIC -fvisibility=hidden -O2 -Wall -Wextra -Werror \
  -o /display.so /src/sdl-display.c /src/menu.c /src/gfx.c /src/cart.c /src/crash.c \
  /src/state.c /src/shader.c /src/shader-gl.c -ldl -lpthread

# GLIBC_2.4 is the armhf base version, so the same ceiling holds here
RUN readelf -h /display.so | grep -q 'ARM$' \
  && readelf -V /display.so | grep -q 'GLIBC_2.4' \
  && ! readelf -V /display.so | grep -Eq 'GLIBC_2\.(1[89]|[2-9][0-9])' \
  && readelf --dyn-syms /display.so | grep -q ' SDL_RenderPresent$' \
  && readelf --dyn-syms /display.so | grep -q ' SDL_RenderCopy$' \
  && readelf --dyn-syms /display.so | grep -q ' SDL_UpdateTexture$' \
  && readelf --dyn-syms /display.so | grep -q ' SDL_PollEvent$' \
  && readelf -W --dyn-syms /display.so | grep -q ' SDL_GameControllerGetButton$' \
  && readelf -W --dyn-syms /display.so | grep -q ' SDL_GameControllerGetAxis$' \
  && readelf --dyn-syms /display.so | grep -q ' SDL_DestroyRenderer$' \
  && readelf -W --dyn-syms /display.so | grep -q ' SDL_OpenAudioDevice$' \
  && readelf --dyn-syms /display.so | grep -q ' SDL_OpenAudio$' \
  && readelf --dyn-syms /display.so | grep -q ' SDL_GetModState$' \
  && readelf --dyn-syms /display.so | grep -q ' SDL_GetTicks$' \
  && readelf -W --dyn-syms /display.so | grep -Eq ' FUNC +GLOBAL +DEFAULT +[0-9]+ malloc$' \
  && readelf -W --dyn-syms /display.so | grep -Eq ' FUNC +GLOBAL +DEFAULT +[0-9]+ calloc$' \
  && readelf -W --dyn-syms /display.so | grep -Eq ' FUNC +GLOBAL +DEFAULT +[0-9]+ realloc$' \
  && readelf -W --dyn-syms /display.so | grep -Eq ' FUNC +GLOBAL +DEFAULT +[0-9]+ free$' \
  && ! readelf -W --dyn-syms /display.so | grep -Eq 'stbi|stbtt|mspace|LZ4| (gfx|menu|cart|crash|state|shader)_' \
  && ! readelf -W --dyn-syms /display.so | grep -Eq ' (sqrt|pow|cos|acos|fmod|floor|ceil|fabs|frexp|ldexp)f?(@|$)' \
  && ! readelf -d /display.so | grep -q 'libm\.so' \
  && ! strings /display.so | grep -q 'libSDL2_image'

# The display shim is run against a fake libSDL2 that records what reaches it,
# as pico-8 would run it against the device's, along with a fake GL for the
# shaders, and the menu is tested on its own with the fonts it finds on the
# device, as are the shader settings. Export takes the shim from these
# stages, so one that fails here never reaches lib/.
FROM build-arm64 AS test-arm64

COPY test/fake-sdl.h test/fake-sdl.c test/display-test.c test/menu-test.c test/shader-test.c \
  /src/test/
COPY --from=fonts /BPreplayBold-unhinted.otf /font1.ttf /fonts/

RUN gcc -shared -fPIC -O2 -Wall -Wextra -Werror -Wl,-soname,libSDL2-2.0.so.0 \
  -o /src/test/libSDL2-2.0.so.0 /src/test/fake-sdl.c \
  && gcc -O2 -Wall -Wextra -Werror -no-pie -o /src/test/display-test /src/test/display-test.c \
  /src/test/libSDL2-2.0.so.0 -Wl,-rpath,/src/test -ldl \
  && gcc -O2 -Wall -Wextra -Werror -o /src/test/menu-test /src/test/menu-test.c \
  /src/menu.c /src/gfx.c /src/cart.c /src/shader.c -lm \
  && gcc -O2 -Wall -Wextra -Werror -o /src/test/shader-test /src/test/shader-test.c /src/shader.c

RUN LD_PRELOAD=/display.so /src/test/display-test \
  && LD_PRELOAD=/display.so /src/test/display-test quit-event \
  && LD_PRELOAD=/display.so /src/test/display-test quit-shortcut \
  && LD_PRELOAD=/display.so /src/test/display-test shaders \
  && /src/test/menu-test /fonts/BPreplayBold-unhinted.otf /fonts/font1.ttf \
  && /src/test/shader-test

# Save states, in runs that share a folder: states saves the slot resume
# puts back in a process of its own, as a device starting a cart would, and
# shutdown and quit save it or not after that. The test is built at a fixed address, as pico8_dyn is, since the
# build's sandbox cannot turn address randomising off for pico8_64's restart.
RUN export PICO_PAK_STATES_DIR=/tmp/display-test-states/states \
  && LD_PRELOAD=/display.so /src/test/display-test states \
  && LD_PRELOAD=/display.so /src/test/display-test resume \
  && LD_PRELOAD=/display.so /src/test/display-test shutdown \
  && LD_PRELOAD=/display.so /src/test/display-test quit

FROM build-arm AS test-arm

COPY test/fake-sdl.h test/fake-sdl.c test/display-test.c test/menu-test.c test/shader-test.c \
  /src/test/
COPY --from=fonts /BPreplayBold-unhinted.otf /font1.ttf /fonts/

RUN gcc -shared -fPIC -O2 -Wall -Wextra -Werror -Wl,-soname,libSDL2-2.0.so.0 \
  -o /src/test/libSDL2-2.0.so.0 /src/test/fake-sdl.c \
  && gcc -O2 -Wall -Wextra -Werror -no-pie -o /src/test/display-test /src/test/display-test.c \
  /src/test/libSDL2-2.0.so.0 -Wl,-rpath,/src/test -ldl \
  && gcc -O2 -Wall -Wextra -Werror -o /src/test/menu-test /src/test/menu-test.c \
  /src/menu.c /src/gfx.c /src/cart.c /src/shader.c -lm \
  && gcc -O2 -Wall -Wextra -Werror -o /src/test/shader-test /src/test/shader-test.c /src/shader.c

RUN LD_PRELOAD=/display.so /src/test/display-test \
  && LD_PRELOAD=/display.so /src/test/display-test quit-event \
  && LD_PRELOAD=/display.so /src/test/display-test quit-shortcut \
  && LD_PRELOAD=/display.so /src/test/display-test shaders \
  && /src/test/menu-test /fonts/BPreplayBold-unhinted.otf /fonts/font1.ttf \
  && /src/test/shader-test

# Save states, in runs that share a folder: states saves the slot resume
# puts back in a process of its own, as a device starting a cart would, and
# shutdown and quit save it or not after that. The test is built at a fixed address, as pico8_dyn is, since the
# build's sandbox cannot turn address randomising off for pico8_64's restart.
RUN export PICO_PAK_STATES_DIR=/tmp/display-test-states/states \
  && LD_PRELOAD=/display.so /src/test/display-test states \
  && LD_PRELOAD=/display.so /src/test/display-test resume \
  && LD_PRELOAD=/display.so /src/test/display-test shutdown \
  && LD_PRELOAD=/display.so /src/test/display-test quit

FROM scratch AS export
COPY --from=build-arm64 /out.so /h700/sdl-nosensor.so
COPY --from=test-arm64 /display.so /arm64/sdl-display.so
COPY --from=test-arm /display.so /arm/sdl-display.so
