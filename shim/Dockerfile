# Builds the SDL sensor shim for h700, which is always a 64-bit userland.
#
# The base image is the one detail that has to be right, and the ceiling matters
# more than the floor: glibc 2.34 folded libdl into libc, so anything built on a
# newer image records dlsym@GLIBC_2.34 and refuses to load on the older glibc
# these devices run. This image is glibc 2.31 and resolves dlsym out of
# libdl.so.2 at GLIBC_2.17, which is the base version on aarch64 and no more
# than bin/h700/minui-presenter already asks for. Anything bookworm based, gcc
# 12 included, is too new.
#
# It also ships gcc already, so the build installs no packages and cannot break
# when Debian moves an end of life release off its mirrors. The assertions below
# are what catch a bump that breaks any of this.
ARG BASE_IMAGE=gcc:10-bullseye

FROM --platform=linux/arm64 ${BASE_IMAGE} AS build-arm64

COPY sdl-nosensor.c /src/sdl-nosensor.c

RUN gcc -shared -fPIC -O2 -Wall -Wextra -Werror \
  -o /out.so /src/sdl-nosensor.c -ldl

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

FROM scratch AS export
COPY --from=build-arm64 /out.so /h700/sdl-nosensor.so
