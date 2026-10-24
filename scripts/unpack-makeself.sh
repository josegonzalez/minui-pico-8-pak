#!/bin/sh
# Unpacks a makeself archive without running any of it, after checking its
# payload against the MD5 the archive records for it.
#
# minui-power-control is released as a makeself archive, which checks and
# unpacks itself into /tmp every time it starts. On a device that took seconds
# of the time a game takes to start, so the pak ships it already unpacked.
#
#   unpack-makeself.sh <archive> <directory>
set -eu

archive="$1"
directory="$2"

# the header is text and the payload follows it, so only the header is read
header_value() {
  LC_ALL=C sed -n "s/^$1=\"\\([^\"]*\\)\"\$/\\1/p" "$archive" | head -n 1
}

md5_of_stdin() {
  if command -v md5sum >/dev/null 2>&1; then
    md5sum | cut -d' ' -f1
  else
    md5 -q
  fi
}

skip="$(header_value skip)"
expected="$(header_value MD5)"
case "$skip" in
"" | *[!0-9]*)
  echo "$archive is not a makeself archive: no line count for its header" 1>&2
  exit 1
  ;;
esac

offset="$(head -n "$skip" "$archive" | wc -c | tr -d ' ')"
actual="$(tail -c +"$((offset + 1))" "$archive" | md5_of_stdin)"
if [ -z "$expected" ] || [ "$expected" = "00000000000000000000000000000000" ] ||
  [ "$actual" != "$expected" ]; then
  echo "$archive does not match its MD5: expected ${expected:-none}, got $actual" 1>&2
  exit 1
fi

rm -rf "$directory"
mkdir -p "$directory"
tail -c +"$((offset + 1))" "$archive" | gzip -cd | tar -xf - -C "$directory"
