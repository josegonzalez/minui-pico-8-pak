#!/usr/bin/env bats

# scripts/unpack-makeself.sh unpacks minui-power-control's release archive at
# build time, so the pak ships it ready to run rather than unpacking it on the
# device every launch.

setup() {
  REPO_ROOT="$(cd "$(dirname "$BATS_TEST_FILENAME")/.." && pwd)"
  WORK="$BATS_TEST_TMPDIR/work"
  mkdir -p "$WORK/payload/arm64"
  printf '#!/bin/sh\necho launched\n' >"$WORK/payload/launch"
  chmod +x "$WORK/payload/launch"
  printf 'binary\n' >"$WORK/payload/arm64/button-handler"
  tar -czf "$WORK/payload.tar.gz" -C "$WORK/payload" .
}

md5_of() {
  if command -v md5sum >/dev/null 2>&1; then
    md5sum <"$1" | cut -d' ' -f1
  else
    md5 -q <"$1"
  fi
}

# Writes an archive laid out as makeself lays one out: a shell header that
# names its own length in lines and its payload's MD5, then the payload.
make_archive() {
  local md5="${1:-$(md5_of "$WORK/payload.tar.gz")}"

  {
    printf '#!/bin/sh\n'
    printf '# This script was generated using Makeself 2.5.0\n'
    printf 'MD5="%s"\n' "$md5"
    printf 'skip="5"\n'
    printf 'exit 0\n'
    cat "$WORK/payload.tar.gz"
  } >"$WORK/archive.run"
}

@test "unpack-makeself unpacks the payload without running the archive" {
  make_archive

  run sh "$REPO_ROOT/scripts/unpack-makeself.sh" "$WORK/archive.run" "$WORK/out"
  [ "$status" -eq 0 ]
  [ -x "$WORK/out/launch" ]
  [ "$(cat "$WORK/out/arm64/button-handler")" = "binary" ]
}

@test "unpack-makeself replaces an earlier unpack" {
  make_archive
  mkdir -p "$WORK/out"
  : >"$WORK/out/stale"

  run sh "$REPO_ROOT/scripts/unpack-makeself.sh" "$WORK/archive.run" "$WORK/out"
  [ "$status" -eq 0 ]
  [ ! -e "$WORK/out/stale" ]
  [ -x "$WORK/out/launch" ]
}

@test "unpack-makeself refuses a payload that does not match its MD5" {
  make_archive 0123456789abcdef0123456789abcdef

  run sh "$REPO_ROOT/scripts/unpack-makeself.sh" "$WORK/archive.run" "$WORK/out"
  [ "$status" -ne 0 ]
  [[ "$output" == *"does not match its MD5"* ]]
  [ ! -e "$WORK/out" ]
}

@test "unpack-makeself refuses an archive that records no MD5" {
  make_archive 00000000000000000000000000000000

  run sh "$REPO_ROOT/scripts/unpack-makeself.sh" "$WORK/archive.run" "$WORK/out"
  [ "$status" -ne 0 ]
  [ ! -e "$WORK/out" ]
}

@test "unpack-makeself refuses a file that is not a makeself archive" {
  printf '#!/bin/sh\necho hello\n' >"$WORK/plain.sh"

  run sh "$REPO_ROOT/scripts/unpack-makeself.sh" "$WORK/plain.sh" "$WORK/out"
  [ "$status" -ne 0 ]
  [[ "$output" == *"is not a makeself archive"* ]]
}
