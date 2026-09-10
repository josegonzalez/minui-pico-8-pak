#!/usr/bin/env bash

# Sets up a hermetic environment and sources launch.sh without running main.
#
# PATH is extended *after* sourcing on purpose: launch.sh puts its own bin
# directories ahead of the inherited PATH, and those hold real ARM binaries
# once `make build` has run.
setup_launch() {
  REPO_ROOT="$(cd "$(dirname "$BATS_TEST_FILENAME")/.." && pwd)"
  export REPO_ROOT

  STUB_BIN="$BATS_TEST_TMPDIR/stub"
  export STUB_BIN
  mkdir -p "$STUB_BIN"
  for stub in minui-presenter killall wget minui-power-control; do
    printf '#!/bin/sh\nexit 0\n' >"$STUB_BIN/$stub"
    chmod +x "$STUB_BIN/$stub"
  done

  export SDCARD_PATH="$BATS_TEST_TMPDIR/sdcard"
  export USERDATA_PATH="$BATS_TEST_TMPDIR/userdata"
  export SHARED_USERDATA_PATH="$BATS_TEST_TMPDIR/shared"
  export LOGS_PATH="$BATS_TEST_TMPDIR/logs"
  mkdir -p "$SDCARD_PATH" "$USERDATA_PATH" "$SHARED_USERDATA_PATH" "$LOGS_PATH"

  CARTS="$BATS_TEST_TMPDIR/carts"
  export CARTS
  mkdir -p "$CARTS"

  export PICO_PAK_SOURCE_ONLY=1
  # shellcheck source=/dev/null
  . "$REPO_ROOT/launch.sh"

  PATH="$STUB_BIN:$PATH"
  export PATH
}

# Creates an empty cart file and echoes its path.
make_cart() {
  local path="$CARTS/$1"
  : >"$path"
  printf '%s' "$path"
}
