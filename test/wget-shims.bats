#!/usr/bin/env bats

# Runs the per-platform wget shims directly, against fakes of the programs they
# wrap, so their contract with pico-8 and the connectivity probe is asserted
# without a device.

setup() {
  REPO_ROOT="$(cd "$(dirname "$BATS_TEST_FILENAME")/.." && pwd)"

  FAKE_BIN="$BATS_TEST_TMPDIR/fake-bin"
  mkdir -p "$FAKE_BIN"
}

# Lays the tg5040 shim out as it is in the pak, next to a wget-original that
# records its library path and arguments, then exits with the given status.
make_tg5040_pak() {
  PAK="$BATS_TEST_TMPDIR/PICO.pak"
  mkdir -p "$PAK/bin/tg5040" "$PAK/lib/tg5040"
  cp "$REPO_ROOT/bin/tg5040/wget" "$PAK/bin/tg5040/wget"

  cat >"$PAK/bin/tg5040/wget-original" <<STUB
#!/bin/sh
printf '%s\n' "\$LD_LIBRARY_PATH" >"$BATS_TEST_TMPDIR/ld_library_path"
printf '%s\n' "\$*" >"$BATS_TEST_TMPDIR/args"
exit ${1:-0}
STUB
  chmod +x "$PAK/bin/tg5040/wget" "$PAK/bin/tg5040/wget-original"
}

# Puts a curl on PATH that records its arguments and exits with the given status.
stub_curl() {
  cat >"$FAKE_BIN/curl" <<STUB
#!/bin/sh
printf '%s\n' "\$*" >"$BATS_TEST_TMPDIR/args"
exit ${1:-0}
STUB
  chmod +x "$FAKE_BIN/curl"
}

@test "tg5040 wget shim puts the pak libraries on LD_LIBRARY_PATH" {
  make_tg5040_pak

  LD_LIBRARY_PATH=/inherited run "$PAK/bin/tg5040/wget" https://example.com/ -q -O /tmp/out
  [ "$status" -eq 0 ]

  lib_dir="$(cd "$(head -n 1 "$BATS_TEST_TMPDIR/ld_library_path" | cut -d: -f1)" && pwd)"
  [ "$lib_dir" = "$(cd "$PAK/lib/tg5040" && pwd)" ]
  [ "$(cut -d: -f2- "$BATS_TEST_TMPDIR/ld_library_path")" = "/inherited" ]
}

@test "tg5040 wget shim does not leave an empty library path element" {
  make_tg5040_pak

  run env -u LD_LIBRARY_PATH "$PAK/bin/tg5040/wget" https://example.com/ -q -O /tmp/out
  [ "$status" -eq 0 ]

  ld_library_path="$(cat "$BATS_TEST_TMPDIR/ld_library_path")"
  [[ "$ld_library_path" != *:* ]]
  [[ "$ld_library_path" == */lib/tg5040 ]]
}

@test "tg5040 wget shim disables certificate checks and passes its arguments" {
  make_tg5040_pak

  run "$PAK/bin/tg5040/wget" https://example.com/ -q -O /tmp/out
  [ "$status" -eq 0 ]
  [ "$(cat "$BATS_TEST_TMPDIR/args")" = "--no-check-certificate https://example.com/ -q -O /tmp/out" ]
}

@test "tg5040 wget shim propagates the exit status of wget-original" {
  make_tg5040_pak 127

  # called directly, as run warns about any 127 it sees
  shim_status=0
  "$PAK/bin/tg5040/wget" https://example.com/ -q -O /tmp/out || shim_status=$?
  [ "$shim_status" -eq 127 ]
}

@test "curl based wget shims fail on http errors" {
  stub_curl

  for platform in h700 my355 rg35xxplus tg5050; do
    PATH="$FAKE_BIN:$PATH" run "$REPO_ROOT/bin/$platform/wget" https://example.com/ -q -O /tmp/out
    [ "$status" -eq 0 ]

    args="$(cat "$BATS_TEST_TMPDIR/args")"
    [[ " $args " == *" -f "* ]]
    [[ "$args" == *" https://example.com/ "* ]]
    [[ "$args" == *"-o /tmp/out" ]]
  done
}

@test "curl based wget shims propagate the exit status of curl" {
  stub_curl 22

  for platform in h700 my355 rg35xxplus tg5050; do
    PATH="$FAKE_BIN:$PATH" run "$REPO_ROOT/bin/$platform/wget" https://example.com/ -q -O /tmp/out
    [ "$status" -eq 22 ]
  done
}
