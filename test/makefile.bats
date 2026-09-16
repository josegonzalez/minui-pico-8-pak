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
  [ "$output" = "0.13.0" ]
}

@test "h700 and tg5050 pull the NextUI presenter builds" {
  mk MINUI_PRESENTER_ASSET_h700
  [ "$output" = "h700-nextui" ]
  mk MINUI_PRESENTER_ASSET_tg5050
  [ "$output" = "tg5050-nextui" ]
}

# the pinned release decides which platforms launch.sh may hand it
@test "the power control gate matches the pinned release" {
  mk MINUI_POWER_CONTROL_VERSION
  [ "$output" = "3.0.0" ]
  [ "$POWER_CONTROL_PLATFORMS" = "miyoomini my355 rg35xxplus tg5040 tg5050" ]
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
  [ "$output" = "0.1.0" ]
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

# a user with an LD_PRELOAD object on their SD card should be able to read what
# it does, while its build recipe is machinery they never run
@test "the sdl shim source ships with the pak but its build recipe does not" {
  run sh -c "cd '$REPO_ROOT' && git check-attr export-ignore shim/sdl-nosensor.c"
  [ "$status" -eq 0 ]
  [[ "$output" == *"export-ignore: unspecified"* ]]

  run sh -c "cd '$REPO_ROOT' && git check-attr export-ignore shim/Dockerfile"
  [ "$status" -eq 0 ]
  [[ "$output" == *"export-ignore: set"* ]]
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
