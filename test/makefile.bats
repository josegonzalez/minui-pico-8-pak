#!/usr/bin/env bats

# Asserts that the three places a platform has to be listed agree with each
# other: pak.json, the Makefile's PLATFORMS, and launch.sh's SUPPORTED_PLATFORMS.
# They have drifted before, which is how a platform shipped without a config.

setup() {
  load test_helper
  setup_launch

  if ! command -v jq >/dev/null 2>&1; then
    skip "jq is not installed"
  fi
}

# Reads a make variable through the print-% target, so the wiring can be
# asserted without a toolchain or a network.
mk() {
  run make --no-print-directory -C "$REPO_ROOT" "print-$1"
  [ "$status" -eq 0 ]
  output="${output#"$1="}"
}

pak_platforms() {
  jq -r '.platforms | join(" ")' "$REPO_ROOT/pak.json"
}

@test "pak.json, the Makefile and launch.sh agree on the platform list" {
  mk PLATFORMS
  [ "$output" = "$(pak_platforms)" ]
  [ "$SUPPORTED_PLATFORMS" = "$(pak_platforms)" ]
}

@test "the platform list is sorted and free of duplicates" {
  platforms="$(jq -r '.platforms[]' "$REPO_ROOT/pak.json")"
  [ "$platforms" = "$(printf '%s\n' "$platforms" | sort -u)" ]
}

@test "every platform's presenter is injected into the release archive" {
  for platform in $(pak_platforms); do
    grep -qx "bin/$platform/minui-presenter" "$REPO_ROOT/.gitarchiveinclude"
  done
}

@test "every platform's wget shim is linted and tracked by git" {
  mk SHELL_FILES
  shell_files="$output"

  for platform in $(pak_platforms); do
    [[ "$shell_files" == *"bin/$platform/wget"* ]]
    run sh -c "cd '$REPO_ROOT' && git ls-files --error-unmatch 'bin/$platform/wget'"
    [ "$status" -eq 0 ]
  done
}

# git archive takes the mode from the index, so a shim recorded as 644 ships
# without its executable bit and command -v never finds it on PATH.
@test "every wget shim is executable in the index" {
  for platform in $(pak_platforms); do
    run sh -c "cd '$REPO_ROOT' && git ls-files -s 'bin/$platform/wget'"
    [ "$status" -eq 0 ]
    [[ "$output" == 100755* ]]
  done
}

@test "the presenter version is new enough to publish an h700 build" {
  mk MINUI_PRESENTER_VERSION
  [ "$output" = "0.14.0" ]
}

@test "h700, my355 and tg5050 pull the NextUI presenter builds" {
  mk MINUI_PRESENTER_ASSET_h700
  [ "$output" = "h700-nextui" ]
  mk MINUI_PRESENTER_ASSET_my355
  [ "$output" = "my355-nextui" ]
  mk MINUI_PRESENTER_ASSET_tg5050
  [ "$output" = "tg5050-nextui" ]
}

# the pinned release decides which platforms launch.sh may hand it
@test "the power control gate matches the pinned release" {
  mk MINUI_POWER_CONTROL_VERSION
  [ "$output" = "3.2.0" ]
  [ "$POWER_CONTROL_PLATFORMS" = "h700 miyoomini my355 rg35xxplus tg5040 tg5050" ]
}

# upstream supports neither h700 nor deep sleep and the lid on a NextUI my355,
# so the release comes from the fork that adds them
@test "power control is pinned to the fork that supports h700 and my355" {
  mk MINUI_POWER_CONTROL_REPO
  [ "$output" = "josegonzalez/minui-power-control" ]
}

# the release is a self-extracting archive that checks and unpacks itself into
# /tmp on every launch, so the build unpacks it once to where launch.sh runs it
@test "power control is unpacked into the folder launch.sh runs it from" {
  run make --no-print-directory -C "$REPO_ROOT" -n -B bin/power-control/launch
  [ "$status" -eq 0 ]
  [[ "$output" == *"scripts/unpack-makeself.sh bin/minui-power-control.run bin/power-control"* ]]

  PAK_DIR="$REPO_ROOT"
  [ "$(get_power_control)" = "$REPO_ROOT/bin/power-control/launch" ]
}

@test "the unpacked power control is injected into the release archive" {
  grep -qx "bin/power-control" "$REPO_ROOT/.gitarchiveinclude"
  ! grep -qx "bin/minui-power-control" "$REPO_ROOT/.gitarchiveinclude"
  run sh -c "cd '$REPO_ROOT' && git check-ignore -q bin/power-control/launch"
  [ "$status" -eq 0 ]
}

@test "the build scripts are linted and do not ship with the pak" {
  mk SHELL_FILES
  [[ " $output " == *" scripts/unpack-makeself.sh "* ]]
  run sh -c "cd '$REPO_ROOT' && git check-attr export-ignore scripts/unpack-makeself.sh"
  [[ "$output" == *"export-ignore: set"* ]]
}

# launch.sh derives the architecture from uname -m and puts bin/$architecture on
# PATH, so anything shipped per architecture has to cover both of its answers.
@test "the architecture list matches what launch.sh can detect" {
  mk ARCHITECTURES
  [ "$output" = "arm arm64" ]
}

@test "every architecture's cart title extractor is injected into the release archive" {
  mk ARCHITECTURES
  for architecture in $output; do
    grep -qx "bin/$architecture/pico8-data-extractor" "$REPO_ROOT/.gitarchiveinclude"
  done
}

@test "the cart title extractor version is pinned" {
  mk PICO8_DATA_EXTRACTOR_VERSION
  [ "$output" = "0.1.1" ]
}

@test "the sdl shim is injected into the release archive" {
  grep -qx "lib/h700/sdl-nosensor.so" "$REPO_ROOT/.gitarchiveinclude"
}

# the shim is compiled from source in this repo, so committing the object would
# create a second copy of it that can silently disagree with the first
@test "the sdl shim is built rather than committed" {
  run sh -c "cd '$REPO_ROOT' && git ls-files --error-unmatch lib/h700/sdl-nosensor.so"
  [ "$status" -ne 0 ]

  run sh -c "cd '$REPO_ROOT' && git check-ignore -q lib/h700/sdl-nosensor.so"
  [ "$status" -eq 0 ]
}

# the device only loads the libraries built from shim/, and the source cannot
# be built from the pak anyway, so it stays in the repository
@test "nothing from the shim folder ships with the pak" {
  run sh -c "cd '$REPO_ROOT' && git archive HEAD | tar -t | grep '^shim/.'"
  [ -z "$output" ]

  run sh -c "cd '$REPO_ROOT' && git ls-files shim | xargs git check-attr export-ignore"
  [ "$status" -eq 0 ]
  [ -n "$output" ]
  ! printf '%s\n' "$output" | grep -qv 'export-ignore: set$'
}

# launch.sh preloads lib/$architecture/sdl-display.so on every platform, so
# each architecture it can pick needs its own build
@test "a display shim is built for every architecture" {
  mk DISPLAY_SHIMS
  [ "$output" = "lib/arm/sdl-display.so lib/arm64/sdl-display.so" ]
}

@test "every architecture's display shim is injected into the release archive" {
  mk ARCHITECTURES
  for architecture in $output; do
    grep -qx "lib/$architecture/sdl-display.so" "$REPO_ROOT/.gitarchiveinclude"
  done
}

@test "the display shim is built rather than committed" {
  mk ARCHITECTURES
  for architecture in $output; do
    run sh -c "cd '$REPO_ROOT' && git ls-files --error-unmatch lib/$architecture/sdl-display.so"
    [ "$status" -ne 0 ]

    run sh -c "cd '$REPO_ROOT' && git check-ignore -q lib/$architecture/sdl-display.so"
    [ "$status" -eq 0 ]
  done
}

# only tg5040 bundles an SDL2_image, and that copy needs the tg5040 system's
# libpng and libjpeg, so the shim decodes overlays itself
@test "the display shim decodes overlays without SDL2_image" {
  ! grep -q 'libSDL2_image' "$REPO_ROOT/shim/sdl-display.c"
  grep -q '#include "stb_image.h"' "$REPO_ROOT/shim/sdl-display.c"
}

# the shim's own tests run in the build, and export takes each object from its
# test stage, so a shim that fails them is never written to lib/
@test "every display shim is exported from a stage that tested it" {
  mk ARCHITECTURES
  for architecture in $output; do
    grep -q "^FROM build-$architecture AS test-$architecture$" "$REPO_ROOT/shim/Dockerfile"
    grep -qx "COPY --from=test-$architecture /display.so /$architecture/sdl-display.so" \
      "$REPO_ROOT/shim/Dockerfile"
  done
}

@test "both architectures run the display and menu tests" {
  mk ARCHITECTURES
  for architecture in $output; do
    awk -v stage="test-$architecture" '
      /^FROM / { in_stage = ($NF == stage) }
      in_stage && /LD_PRELOAD=\/display.so \/src\/test\/display-test/ { display = 1 }
      in_stage && /\/src\/test\/menu-test \/fonts\// { menu = 1 }
      END { exit !(display && menu) }' "$REPO_ROOT/shim/Dockerfile"
  done
}

# the menu draws in the device's own font, which only the test stages get
@test "the menu is tested against both launchers' fonts without shipping them" {
  grep -Eq 'shauninman/MinUI/[0-9a-f]{40}/skeleton/SYSTEM/res/BPreplayBold-unhinted.otf' \
    "$REPO_ROOT/shim/Dockerfile"
  grep -Eq 'LoveRetro/NextUI/[0-9a-f]{40}/skeleton/SYSTEM/res/font1.ttf' "$REPO_ROOT/shim/Dockerfile"

  # the export stage is what make writes into lib/, and it copies objects alone
  run sh -c "sed -n '/ AS export\$/,\$p' '$REPO_ROOT/shim/Dockerfile' | grep '^COPY'"
  [ "$status" -eq 0 ]
  [ -n "$output" ]
  ! printf '%s\n' "$output" | grep -qv '\.so$'
}

# an edit to the tests or the menu has to rebuild the shims, or a stale object
# skips them
@test "the display shim sources and tests are prerequisites of the shim build" {
  for prerequisite in shim/menu.c shim/menu.h shim/gfx.c shim/gfx.h shim/cart.c shim/cart.h \
    shim/crash.c shim/crash.h \
    shim/test/display-test.c \
    shim/test/menu-test.c shim/test/fake-sdl.c; do
    run make --no-print-directory -C "$REPO_ROOT" -n -W "$prerequisite" lib/arm64/sdl-display.so
    [ "$status" -eq 0 ]
    [[ "$output" == *"docker buildx build"* ]]
  done
}

@test "the display shim tests do not ship with the pak" {
  for test_file in display-test.c menu-test.c; do
    run sh -c "cd '$REPO_ROOT' && git check-attr export-ignore shim/test/$test_file"
    [ "$status" -eq 0 ]
    [[ "$output" == *"export-ignore: set"* ]]
  done
}

@test "the png and font decoders are pinned by commit and checksum" {
  for header in stb_image.h stb_truetype.h stb_image_write.h; do
    grep -B1 "nothings/stb/[0-9a-f]\{40\}/$header" "$REPO_ROOT/shim/Dockerfile" |
      grep -Eq '^ADD --checksum=sha256:[0-9a-f]{64} '
  done
}

# a function of the menu's bound to one of the same name in pico-8 would call
# into pico-8 instead, and malloc's family is exported for the save state arena
@test "the display shim exports only its SDL wrappers and malloc's family" {
  [ "$(grep -c -- '-fvisibility=hidden' "$REPO_ROOT/shim/Dockerfile")" -eq 2 ]
  grep -q "readelf -W --dyn-syms /display.so | grep -Eq 'stbi|stbtt|mspace| (gfx|menu|cart|crash|state)_'" \
    "$REPO_ROOT/shim/Dockerfile"
  [ "$(grep -c -- 'DEFAULT +\[0-9\]+ malloc\$' "$REPO_ROOT/shim/Dockerfile")" -eq 2 ]
}

# glibc 2.34 folded libdl into libc, so a newer base records dlsym@GLIBC_2.34
# and the shim stops loading on the device
@test "the sdl shim base image is pinned below the libdl merge" {
  mk SHIM_IMAGE
  [ "$output" = "gcc:10-bullseye" ]
}

@test "the sdl shim masks only the sensor flag" {
  grep -q '0x00008000' "$REPO_ROOT/shim/sdl-nosensor.c"
  ! grep -Eq 'SDL_INIT_(VIDEO|AUDIO|JOYSTICK|GAMECONTROLLER|HAPTIC|TIMER|EVENTS)' \
    "$REPO_ROOT/shim/sdl-nosensor.c"
}
