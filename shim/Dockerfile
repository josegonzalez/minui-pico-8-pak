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

# The display shim decodes overlays itself rather than relying on an SDL2_image
# the device may not have, so the decoder is pinned by commit and checksum.
FROM scratch AS stb
ADD --checksum=sha256:594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3 \
  https://raw.githubusercontent.com/nothings/stb/003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5/stb_image.h \
  /stb_image.h

FROM --platform=linux/arm64 ${BASE_IMAGE} AS build-arm64

COPY sdl-nosensor.c /src/sdl-nosensor.c
COPY sdl-display.c /src/sdl-display.c
COPY --from=stb /stb_image.h /src/stb_image.h

RUN gcc -shared -fPIC -O2 -Wall -Wextra -Werror \
  -o /out.so /src/sdl-nosensor.c -ldl

RUN gcc -shared -fPIC -O2 -Wall -Wextra -Werror \
  -o /display.so /src/sdl-display.c -ldl

# The display shim has to export every wrapper and nothing of the decoder, and
# must not go looking for an SDL2_image at runtime.
RUN readelf -V /display.so | grep -q 'GLIBC_2.17' \
  && ! readelf -V /display.so | grep -Eq 'GLIBC_2\.(1[89]|[2-9][0-9])' \
  && readelf --dyn-syms /display.so | grep -q ' SDL_RenderPresent$' \
  && readelf --dyn-syms /display.so | grep -q ' SDL_RenderCopy$' \
  && readelf --dyn-syms /display.so | grep -q ' SDL_PollEvent$' \
  && readelf -W --dyn-syms /display.so | grep -q ' SDL_GameControllerGetButton$' \
  && readelf --dyn-syms /display.so | grep -q ' SDL_DestroyRenderer$' \
  && ! readelf -W --dyn-syms /display.so | grep -q 'stbi' \
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

COPY sdl-display.c /src/sdl-display.c
COPY --from=stb /stb_image.h /src/stb_image.h

RUN gcc -shared -fPIC -O2 -Wall -Wextra -Werror \
  -o /display.so /src/sdl-display.c -ldl

# GLIBC_2.4 is the armhf base version, so the same ceiling holds here
RUN readelf -h /display.so | grep -q 'ARM$' \
  && readelf -V /display.so | grep -q 'GLIBC_2.4' \
  && ! readelf -V /display.so | grep -Eq 'GLIBC_2\.(1[89]|[2-9][0-9])' \
  && readelf --dyn-syms /display.so | grep -q ' SDL_RenderPresent$' \
  && readelf --dyn-syms /display.so | grep -q ' SDL_RenderCopy$' \
  && readelf --dyn-syms /display.so | grep -q ' SDL_PollEvent$' \
  && readelf -W --dyn-syms /display.so | grep -q ' SDL_GameControllerGetButton$' \
  && readelf --dyn-syms /display.so | grep -q ' SDL_DestroyRenderer$' \
  && ! readelf -W --dyn-syms /display.so | grep -q 'stbi' \
  && ! strings /display.so | grep -q 'libSDL2_image'

# The display shim is run against a fake libSDL2 that records what reaches it,
# as pico-8 would run it against the device's, once for each Menu button
# launch.sh can name. Export takes the shim from these stages, so one that
# fails here never reaches lib/.
FROM build-arm64 AS test-arm64

COPY test/fake-sdl.h test/fake-sdl.c test/display-test.c /test/

RUN gcc -shared -fPIC -O2 -Wall -Wextra -Werror -Wl,-soname,libSDL2-2.0.so.0 \
  -o /test/libSDL2-2.0.so.0 /test/fake-sdl.c \
  && gcc -O2 -Wall -Wextra -Werror -o /test/display-test /test/display-test.c \
  /test/libSDL2-2.0.so.0 -Wl,-rpath,/test

RUN for menu in guide start none; do \
  LD_PRELOAD=/display.so /test/display-test "$menu" || exit 1; \
  done

FROM build-arm AS test-arm

COPY test/fake-sdl.h test/fake-sdl.c test/display-test.c /test/

RUN gcc -shared -fPIC -O2 -Wall -Wextra -Werror -Wl,-soname,libSDL2-2.0.so.0 \
  -o /test/libSDL2-2.0.so.0 /test/fake-sdl.c \
  && gcc -O2 -Wall -Wextra -Werror -o /test/display-test /test/display-test.c \
  /test/libSDL2-2.0.so.0 -Wl,-rpath,/test

RUN for menu in guide start none; do \
  LD_PRELOAD=/display.so /test/display-test "$menu" || exit 1; \
  done

FROM scratch AS export
COPY --from=build-arm64 /out.so /h700/sdl-nosensor.so
COPY --from=test-arm64 /display.so /arm64/sdl-display.so
COPY --from=test-arm /display.so /arm/sdl-display.so
