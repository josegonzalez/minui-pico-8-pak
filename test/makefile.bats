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
