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
  mkdir -p "$SDCARD_PATH/Roms" "$USERDATA_PATH/Pico-8-native" "$SHARED_USERDATA_PATH/Pico-8-native"

  CARTS="$BATS_TEST_TMPDIR/carts"
  export CARTS
  mkdir -p "$CARTS"

  # An empty tree is the baseline, so the suite never reads the host's real
  # network state. macOS has no /sys/class/net at all and the CI runner has a
  # populated one, which would otherwise make the link check non-deterministic.
  PICO_PAK_NET_DIR="$BATS_TEST_TMPDIR/net"
  export PICO_PAK_NET_DIR
  mkdir -p "$PICO_PAK_NET_DIR"

  export PICO_PAK_SOURCE_ONLY=1
  # shellcheck source=/dev/null
  . "$REPO_ROOT/launch.sh"

  # launch.sh derives these from "$0", which is the bats runner when the file is
  # sourced rather than executed. Point them back at the repo so the functions
  # under test resolve real pak files.
  PAK_DIR="$REPO_ROOT"
  PAK_NAME="PICO"
  export PAK_DIR PAK_NAME

  PATH="$STUB_BIN:$PATH"
  export PATH
}

# Creates an empty cart file and echoes its path.
make_cart() {
  local path="$CARTS/$1"
  : >"$path"
  printf '%s' "$path"
}

# Creates a roms folder under the fake SD card and echoes its path.
make_rom_folder() {
  local path="$SDCARD_PATH/Roms/$1"
  mkdir -p "$path"
  printf '%s' "$path"
}

# Creates a fake sysfs network interface with the given operstate. The interface
# defaults to wlan0. Call it on top of the empty tree setup_launch builds.
stub_network() {
  local state="$1"
  local interface="${2:-wlan0}"

  mkdir -p "$PICO_PAK_NET_DIR/$interface"
  if [ -n "$state" ]; then
    printf '%s\n' "$state" >"$PICO_PAK_NET_DIR/$interface/operstate"
  fi
}

# Replaces the minui-presenter stub with one that logs its arguments and exits
# with the given code, where 0 confirms a dialog and non-zero cancels it.
#
# show_message backgrounds the presenter, so logging every call to one file
# races with assertions made after the function under test returns. Only the
# blocking dialog passes --confirm-show, and that call runs in the foreground,
# so it is logged separately and can be asserted on deterministically.
#
# Arguments are logged as "$*" on a single line so that flag and value
# adjacency can be asserted, for example "--confirm-text CONTINUE".
stub_presenter() {
  PRESENTER_LOG="$BATS_TEST_TMPDIR/presenter.log"
  PRESENTER_CONFIRM_LOG="$BATS_TEST_TMPDIR/presenter.confirm.log"
  PRESENTER_EXIT="$BATS_TEST_TMPDIR/presenter.exit"
  export PRESENTER_LOG PRESENTER_CONFIRM_LOG PRESENTER_EXIT
  printf '%s' "${1:-0}" >"$PRESENTER_EXIT"

  cat >"$STUB_BIN/minui-presenter" <<'STUB'
#!/bin/sh
for arg in "$@"; do
  if [ "$arg" = "--confirm-show" ]; then
    printf '%s\n' "$*" >>"$PRESENTER_CONFIRM_LOG"
    exit "$(cat "$PRESENTER_EXIT")"
  fi
done
printf '%s\n' "$*" >>"$PRESENTER_LOG"
exit 0
STUB
  chmod +x "$STUB_BIN/minui-presenter"
}

# Replaces the wget stub with one that logs its arguments and reproduces one of
# the outcomes the platform shims produce:
#
#   reachable    a body is written and the fetch succeeds
#   unreachable  an empty file is left behind and the fetch fails
#   empty        an empty file is left behind but the fetch reports success
#   silent       nothing is written at all and the fetch reports success
#
# The output path is read from $4 to match the positional contract of the curl
# based shims on rg35xxplus and tg5050.
stub_wget() {
  WGET_LOG="$BATS_TEST_TMPDIR/wget.log"
  WGET_MODE="$BATS_TEST_TMPDIR/wget.mode"
  export WGET_LOG WGET_MODE
  printf '%s' "${1:-reachable}" >"$WGET_MODE"

  cat >"$STUB_BIN/wget" <<'STUB'
#!/bin/sh
printf '%s\n' "$*" >>"$WGET_LOG"
mode="$(cat "$WGET_MODE")"
if [ "$mode" = "reachable" ]; then
  printf 'User-agent: *\n' >"$4"
  exit 0
fi
if [ "$mode" = "empty" ]; then
  : >"$4"
  exit 0
fi
if [ "$mode" = "silent" ]; then
  exit 0
fi
: >"$4"
exit 1
STUB
  chmod +x "$STUB_BIN/wget"
}

# Puts a recording timeout on PATH so the bounded probe can be asserted on hosts
# that do not ship one. Opt in per test and never remove it mid-test: dropping a
# command from PATH leaves a stale entry in the shell's command hash table, so
# `command -v` keeps succeeding while the exec fails.
stub_timeout() {
  TIMEOUT_LOG="$BATS_TEST_TMPDIR/timeout.log"
  export TIMEOUT_LOG

  cat >"$STUB_BIN/timeout" <<'STUB'
#!/bin/sh
printf '%s\n' "$*" >>"$TIMEOUT_LOG"
shift
exec "$@"
STUB
  chmod +x "$STUB_BIN/timeout"
}

# Creates the files install_pico_files requires, so end to end tests reach the
# checks that run after it.
make_bios() {
  mkdir -p "$SDCARD_PATH/Bios/PICO"
  for bios in pico8 pico8_64 pico8_dyn pico8.dat; do
    : >"$SDCARD_PATH/Bios/PICO/$bios"
  done
}

# Creates a writable fake cpufreq policy directory and points launch.sh at it.
# scaling_setspeed starts empty so a test can tell "not written" from "written".
stub_cpufreq() {
  PICO_PAK_CPUFREQ_DIR="$BATS_TEST_TMPDIR/cpufreq"
  export PICO_PAK_CPUFREQ_DIR

  mkdir -p "$PICO_PAK_CPUFREQ_DIR"
  : >"$PICO_PAK_CPUFREQ_DIR/scaling_setspeed"
}

# Puts an fbset reporting the given geometry on PATH, so the panel lookup can be
# exercised on hosts that have no framebuffer. Defaults to the tg5040 panel.
#
# Never remove it mid-test: dropping a command from PATH leaves a stale entry in
# the shell's command hash table, so `command -v` keeps succeeding.
stub_fbset() {
  local width="${1:-1280}"
  local height="${2:-720}"

  cat >"$STUB_BIN/fbset" <<STUB
#!/bin/sh
echo "mode \"${width}x${height}\""
echo "    geometry $width $height $width $((height * 2)) 32"
echo "endmode"
STUB
  chmod +x "$STUB_BIN/fbset"
}
